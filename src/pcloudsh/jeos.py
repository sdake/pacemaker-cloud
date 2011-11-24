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
from pcloudsh import db_helper

class Jeos(object):

    def __init__(self, factory, name, arch):
        self.conf = pcmkconfig.Config()
        self.factory = factory

        query = factory.doc.xpathEval("/%s/%s[@name='%s']" % (self.factory.plural,
            self.factory.singular, name))

        self.xml_node = None
        if (len(query)):
            self.xml_node = query[0]
            self.name = self.xml_node.prop("name")
            self.arch = self.xml_node.prop("arch")
            self.tdl_path = self.xml_node.prop("tdl_path")
            self.xml_path = self.xml_node.prop("xml_path")
        else:
            self.factory = factory
            self.name = name
            self.arch = arch
            self.xml_path = '%s/jeos/%s-%s-jeos.xml' % (self.conf.dbdir, self.name, self.arch)
            self.tdl_path = '%s/jeos/%s-%s-jeos.tdl' % (self.conf.dbdir, self.name, self.arch)

        self.dsk_filename = '/var/lib/libvirt/images/%s-%s-jeos.dsk' % (self.name, self.arch)
        self.qcow2_filename = '/var/lib/libvirt/images/%s-%s-jeos.qcow2' % (self.name, self.arch)

    def save(self):
        if self.xml_node is None:
            node = self.factory.root_get().newChild(None, self.factory.singular, None)
            node.newProp("name", self.name)
            node.newProp("arch", self.arch)
            node.newProp("tdl_path", self.tdl_path)
            node.newProp("xml_path", self.xml_path)
            self.xml_node = node
        else:
            self.xml_node.setProp('name', self.name)
            self.xml_node.setProp('arch', self.arch)
            self.xml_node.setProp('tdl_path', self.tdl_path)
            self.xml_node.setProp('xml_path', self.xml_path)

        self.factory.save()

    def create(self):

        iso = None
        if self.name == 'F16':
            iso = '/var/lib/libvirt/images/Fedora-16-x86_64-DVD.iso'
        if self.name == 'F15':
            iso = '/var/lib/libvirt/images/Fedora-15-x86_64-DVD.iso'
        if self.name == 'F14':
            iso = '/var/lib/libvirt/images/Fedora-14-x86_64-DVD.iso'
        if iso:
            if not os.access(iso, os.R_OK):
                print '*** %s does not exist.' % (iso)
                return False

        res = os.system("oz-install -t 50000 -u -d3 -x %s %s" % (self.xml_path, self.tdl_path))
        if res == 256:
            return False

        os.system("qemu-img convert -O qcow2 %s %s" % (self.dsk_filename, self.qcow2_filename))

        libvirt_xml = libxml2.parseFile(self.xml_path)
        source_xml = libvirt_xml.xpathEval('/domain/devices/disk')
        driver = source_xml[0].newChild (None, "driver", None)
        driver.newProp ("type", "qcow2")
        source_xml = libvirt_xml.xpathEval('/domain/devices/disk/source')
        source_xml[0].setProp('file', self.qcow2_filename)
        source_xml = libvirt_xml.xpathEval('/domain/devices/serial')
        root_node = source_xml[0]
        root_node.unlinkNode()
        libvirt_xml.saveFormatFile(self.xml_path, format=1)
        return True

    def delete(self):

        if os.access(self.xml_path, os.R_OK):
            os.unlink(self.xml_path)
            print ' deleted jeos virt xml'

        if os.access(self.dsk_filename, os.R_OK):
            os.unlink(self.dsk_filename)
            print ' deleted jeos disk image'

        if os.access(self.qcow2_filename, os.R_OK):
            os.unlink(self.qcow2_filename)
            print ' deleted jeos qcow2 image'

class JeosFactory(db_helper.DbFactory):

    def __init__(self):
        db_helper.DbFactory.__init__(self, 'db_jeos.xml', 'images', 'jeos')

    def __str__(self):
        return '%s_%s' % (self.name, self.arch)

    def make_id(self, name, arch):
        return '%s_%s' % (name, arch)

    def load(self):
        list = self.doc.xpathEval("/%s/%s" % (self.plural, self.singular))
        for node in list:
            n = self.make_id(node.prop('name'), node.prop('arch'))
            if n not in self.all:
                self.all[n] = Jeos(self, node.prop('name'), node.prop('arch'))

    def delete(self, name, arch):

        self.get(name, arch).delete()
        node_list = self.doc.xpathEval("/%s/%s[@name='%s']" % (self.plural, self.singular, name))
        for node in node_list:
            if node.prop('name') == name and node.prop('arch') == arch:
                print ' deleted from xml db'
                node.unlinkNode()
                self.save()
        del self.all[self.make_id(name, arch)]

    def create(self, name, arch):
        n = self.make_id(name, arch)
        if n in self.all:
            print ' *** Jeos already exists.'
            return
        else:
            j = Jeos(self, name, arch)
            if j.create():
                self.all[n] = j
                j.save()
            else:
                self.delete(name, arch)

    def exists(self, name, arch):
        n = self.make_id(name, arch)
        if n in self.all:
            return True
        else:
            return False

    def get(self, name, arch='x86_64'):
        n = self.make_id(name, arch)
        if n in self.all:
            return self.all[n]

        self.all[n] = Jeos(self, name, arch)
        return self.all[n]
