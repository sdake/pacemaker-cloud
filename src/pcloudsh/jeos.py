#
# Copyright (C) 2011 Red Hat, Inc.
#
# Author: Steven Dake <sdake@redhat.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#
import os
import sys
import time
import re
import random
import logging
import libxml2
import exceptions
import libvirt
from pcloudsh import pcmkconfig

class Jeos(object):

    def __init__(self):
        self.conf = pcmkconfig.Config()
        self.xml_file = '%s/db_jeos.xml' % (self.conf.dbdir)
        try:
            self.doc = libxml2.parseFile(self.xml_file)
            self.doc_images = self.doc.getRootElement()
        except:
            self.doc = libxml2.newDoc("1.0")
            self.doc.newChild(None, "images", None)
            self.doc_images = self.doc.getRootElement()
            self.doc_images.setProp('pcmkc-version', pcmkconfig.version)

    def create(self, name, arch):
        jeos_list = self.doc.xpathEval("/images/jeos")
        for jeos_data in jeos_list:
            if jeos_data.prop('name') == name and jeos_data.prop('arch') == arch:
                raise
        xml_filename = '%s/jeos/%s-%s-jeos.xml' % (self.conf.dbdir, name, arch)
        tdl_filename = '%s/jeos/%s-%s-jeos.tdl' % (self.conf.dbdir, name, arch)
        dsk_filename = '/var/lib/libvirt/images/%s-%s-jeos.dsk' % (name, arch)
        qcow2_filename = '/var/lib/libvirt/images/%s-%s-jeos.qcow2' % (name, arch)

        res = os.system("oz-install -t 50000 -u -d3 -x %s %s" % (xml_filename, tdl_filename))
        if res == 256:
            raise

        os.system("qemu-img convert -O qcow2 %s %s" % (dsk_filename, qcow2_filename))

        libvirt_xml = libxml2.parseFile(xml_filename)
        source_xml = libvirt_xml.xpathEval('/domain/devices/disk')
        driver = source_xml[0].newChild (None, "driver", None)
        driver.newProp ("type", "qcow2")
        source_xml = libvirt_xml.xpathEval('/domain/devices/disk/source')
        source_xml[0].setProp('file', qcow2_filename)
        libvirt_xml.saveFormatFile(xml_filename, format=1)

        doc_jeos = self.doc_images.newChild(None, "jeos", None)
        doc_xml_path = doc_jeos.newProp("arch", arch)
        doc_xml_path = doc_jeos.newProp("name", name)
        doc_tdl_path = doc_jeos.newProp("tdl_path", xml_filename)
        doc_xml_path = doc_jeos.newProp("xml_path", xml_filename)

        self.doc.saveFormatFile(self.xml_file, format=1)

    def list(self, listiter):
        jeos_list = self.doc.xpathEval("/images/jeos")
        for jeos_data in jeos_list:
            listiter.append("%s %s" % (jeos_data.prop('name'), jeos_data.prop('arch')))
