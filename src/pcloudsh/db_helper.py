#
# Copyright (C) 2011 Red Hat, Inc.
#
# Author: Angus Salkeld <asalkeld@redhat.com>
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

from pcloudsh import pcmkconfig

class DbFactory(object):

    def __init__(self, filename, plural, singular):
        self.conf = pcmkconfig.Config()
        self.plural = plural
        self.singular = singular
        self.xml_file = '%s/%s' % (self.conf.dbdir, filename)
        it_exists = os.access(self.xml_file, os.R_OK)
        libxml2.keepBlanksDefault(False)

        try:
            if it_exists:
                self.doc = libxml2.parseFile(self.xml_file)
        except:
            it_exists = False

        if not it_exists:
            self.doc = libxml2.newDoc("1.0")
            self.root_node = self.doc.newChild(None, plural, None)
            self.root_node.setProp('pcmkc-version', self.conf.version)
        else:
            self.root_node = self.doc.getRootElement()

        _ver = self.root_node.prop('pcmkc-version')
        if not _ver == self.conf.version:
            _msg = '*** warning xml and program version mismatch'
            print '%s \"%s\" != \"%s\"' % (_msg, _ver, self.conf.version)

        self.all = {}
        self.load()

    def load(self):
        list = self.doc.xpathEval("/%s/%s" % (self.plural, self.singular))
        for node in list:
            n = node.prop('name')
            if n not in self.all:
                self.all[n] = self.create_instance(n)

    def root_get(self):
        return self.root_node

    def save(self):
        self.doc.saveFormatFile(self.xml_file, format=1)

    def all_get(self):
        return self.all.values()

    def exists(self, name):
        if name in self.all:
            return True
        else:
            return False

    def get(self, name):
        if name in self.all:
            return self.all[name]

        self.all[name] = self.create_instance(name)
        return self.all[name]

    def create_instance(self, name):
        raise

    def delete_instance(self, name):
        node_list = self.doc.xpathEval("/%s/%s[@name='%s']" % (self.plural, self.singular, name))
        if len(node_list) == 0:
            print '*** %s %s does not exist.' % (self.singular, name)
            return
        first_node = node_list[0]
        first_node.unlinkNode()
        del self.all[name]
        self.save()
