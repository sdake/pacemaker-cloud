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
import time
import re
import random
import logging
import libxml2
import exceptions

class Jeos(object):

    def __init__(self):
        try:
            self.doc = libxml2.parseFile('db_jeos.xml')
            self.doc_images = self.doc.getRootElement()
        except:
            self.doc = libxml2.newDoc("1.0")
            self.doc.newChild(None, "images", None);
            self.doc_images = self.doc.getRootElement()

    def create(self, name, arch):
        jeos_list = self.doc.xpathEval("/images/jeos")
        for jeos_data in jeos_list:
            if jeos_data.prop('name') == name and jeos_data.prop('arch') == arch:
                raise
        xml_filename = '%s-%s-jeos.xml' % (name, arch)
        tdl_filename = '%s-%s-jeos.tdl' % (name, arch)

        res = os.system("oz-install -t 50000 -u -d3 -x %s %s" % (xml_filename, tdl_filename));
        if res == 256:
            raise

        doc_jeos = self.doc_images.newChild(None, "jeos", None);
        doc_xml_path = doc_jeos.newProp("arch", arch);
        doc_xml_path = doc_jeos.newProp("name", name);
        doc_tdl_path = doc_jeos.newProp("tdl_path", xml_filename);
        doc_xml_path = doc_jeos.newProp("xml_path", xml_filename);
        self.doc.serialize(None, 1)
        self.doc.saveFile('db_jeos.xml');

    def list(self, listiter):
        jeos_list = self.doc.xpathEval("/images/jeos")
        for jeos_data in jeos_list:
            listiter.append("%s %s" % (jeos_data.prop('name'), jeos_data.prop('arch')))
