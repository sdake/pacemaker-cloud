#
# Copyright (C) 2011 Red Hat, Inc.
#
# Author: Steven Dake <sdake@redhat.com>
#         Angus Salkeld <asalkeld@redhat.com>
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
import libxml2
import exceptions

class Resource(object):

    def __init__(self, factory):
        self.factory = factory
        self.name = ''
        self.type = 'http'
        self.klass = 'lsb'
        self.monitor_interval = '60s'
        self.xml_node = None

    def load_from_xml(self, xml):
        self.xml_node = xml
        self.name = xml.prop('name')
        self.monitor_interval = xml.prop('monitor_interval')
        self.type = xml.prop('type')
        self.klass = xml.prop('class')

    def save(self):
        if self.xml_node is None:
            nd = libxml2.parseFile('/usr/share/pacemaker-cloud/resource_templates/%s.xml' % self.type)
            n = nd.getRootElement()
            n.newProp('name', self.name)
            self.factory.root_get().addChild(n)
            self.xml_node = n
        else:
            self.xml_node.setProp('name', self.name)
            self.xml_node.setProp('type', self.type)
            self.xml_node.setProp('class', self.klass)
            self.xml_node.setProp('monitor_interval', self.monitor_interval)

    def name_set(self, name):
        self.name = name

    def class_set(self, c):
        self.klass = c

    def type_set(self, t):
        self.type = t

    def monitor_interval_set(self, monitor_interval):
        self.monitor_interval = monitor_interval

    def name_get(self):
        return self.name

    def type_get(self):
        return self.type

    def monitor_interval_get(self):
        return self.monitor_interval

    def __str__(self):
        return 'resource: %s_%s' % (self.name, self.monitor_interval)

class ResourceFactory(object):

    def __init__(self, assembly_node):
        self.all = {}

        self.root_node = None
        if assembly_node.children != None:
            for n in assembly_node.children:
                if n.name == 'resources':
                    self.root_node = n

        if self.root_node is None:
            self.root_node = assembly_node.newChild(None, "resources", None)

        if self.root_node.children != None:
            for r in self.root_node.children:
                n = r.prop('name')
                if n not in self.all:
                    self.all[n] = Resource(self)
                    self.all[n].load_from_xml(r)

    def root_get(self):
        return self.root_node

    def exists(self, name):
        if name in self.all:
            return True
        else:
            return False

    def get(self, name):
        if name in self.all:
            return self.all[name]

        a = Resource(self)
        a.name_set(name)
        self.all[name] = a
        return a

    def delete(self, name):
        self.root_node.unlinkNode();
        self.root_node = None

    def save(self):
        pass

    def all_get(self):
        return self.all

