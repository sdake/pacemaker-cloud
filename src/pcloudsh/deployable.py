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
import libvirt
import re
import random
import libxml2
import shutil
import exceptions

from pcloudsh import cpe
from pcloudsh import assembly_factory
from pcloudsh import pcmkconfig
from pcloudsh import db_helper


class DeployableDb(object):

    def __init__(self, factory, name):
        self.conf = pcmkconfig.Config()
        self.factory = factory
        self.l = factory.l
        self.monitor = None

        query = factory.doc.xpathEval("/%s/%s[@name='%s']" % (self.factory.plural,
            self.factory.singular, name))

        if (len(query)):
            self.xml_node = query[0]
            self.name = self.xml_node.prop("name")
            self.username = self.xml_node.prop("username")
            if self.xml_node.hasProp('monitor'):
                self.monitor = self.xml_node.prop("monitor")
            else:
                self.monitor = 'active'
        else:
            self.xml_node = None
            self.name = name
            if self.username == None:
                self.username = 'root'

    def save(self):
        if self.xml_node is None:
            node = self.factory.root_get().newChild(None, self.factory.singular, None)
            node.newProp("name", self.name)
            node.newProp("infrastructure", self.infrastructure)
            node.newProp("username", self.username)
            node.newProp("monitor", self.monitor)
            self.xml_node = node
        else:
            self.xml_node.setProp('name', self.name)
            self.xml_node.setProp('infrastructure', self.infrastructure)
            self.xml_node.setProp('username', self.username)
            self.xml_node.setProp('monitor', self.monitor)

        self.factory.save()

    def assembly_add(self, aname, fac=None):
        if not fac:
            fac = assembly_factory.AssemblyFactory(self.factory.l)
        if not fac.exists(aname):
            print '*** Assembly %s does not exist' % (aname)
            return

        self.save()
        if self.xml_node.children != None:
            for c in self.xml_node.children:
                if c.hasProp('name') and c.prop('name') == aname:
                    print '*** Assembly %s is already in Deployable %s' % (aname, self.name)
                    return
        assembly_root = self.xml_node.newChild(None, "assembly", None)
        assembly_root.newProp("name", aname)
        self.save()

        fac.register(aname, self.infrastructure, self.name, self.username)

    def assembly_remove(self, aname, fac=None):
        if not fac:
            fac = assembly_factory.AssemblyFactory(self.factory.l)
        if not fac.exists(aname):
            print '*** Assembly %s does not exist' % (aname)
            return
        ass = fac.get(aname)
        ass.deployment = None
        ass.save()

        self.save()
        if self.xml_node.children != None:
            for c in self.xml_node.children:
                if c.hasProp('name') and c.prop('name') == aname:
                    c.unlinkNode()
                    self.save()
                    return

        print '*** Assembly %s is not in Deployable %s' % (aname, self.name)


    def assembly_list_get(self, fac=None):
        if not fac:
            fac = assembly_factory.AssemblyFactory(self.factory.l)
        al = []
        if self.xml_node.children != None:
            for c in self.xml_node.children:
                if c.hasProp('name'):
                    al.append(fac.get(c.prop('name')))
        return al


    def generate_config(self):

        fac = assembly_factory.AssemblyFactory(self.factory.l)

        doc = libxml2.newDoc("1.0")
        dep = doc.newChild(None, "deployable", None)
        dep.setProp("name", self.name)
        dep.setProp("uuid", self.name) # TODO
        dep.setProp("monitor", self.monitor)
        dep.setProp("username", self.username)
        n_asses = dep.newChild(None, "assemblies", None)
        constraints = dep.newChild(None, 'constraints', None)

        a_list = self.factory.doc.xpathEval("/deployables/deployable[@name='%s']/assembly" % self.name)
        for a_data in a_list:
            a = fac.get(a_data.prop('name'))

            n_ass = n_asses.newChild(None, 'assembly', None)
            n_ass.setProp("name", a.name)
            n_ass.setProp("uuid", a.uuid)
            n_ass.setProp("escalation_failures", a.escalation_failures)
            n_ass.setProp("escalation_period", a.escalation_period)
            n_servs = n_ass.newChild(None, "services", None)

            for r in a.resources_get():
                n_srv = n_servs.newChild(None, 'service', None)
                n_srv.setProp("name", r.name)
                n_srv.setProp("provider", r.provider)
                n_srv.setProp("class", r.klass)
                n_srv.setProp("type", r.type)
                n_srv.setProp("monitor_interval", r.monitor_interval)
                n_srv.setProp('escalation_period',   r.escalation_period)
                n_srv.setProp('escalation_failures', r.escalation_failures)

                if len(r.params) > 0:
                    n_ps = n_srv.newChild(None, 'paramaters', None)
                    for p in r.params.keys():
                        n_p = n_ps.newChild(None, 'paramater', None)
                        n_p.setProp("name", p)
                        n_p.setProp("value", r.params[p])

        filename = '/var/run/%s.xml' % self.name
        open(filename, 'w').write(doc.serialize(None, 1))
        doc.freeDoc()


class Deployable(DeployableDb):

    def __init__(self, factory, name):
        DeployableDb.__init__(self, factory, name)
        self.cpe = cpe.Cpe(self.factory.l)

    def delete(self):
        pass

    def create(self):
        return True

    def start(self):
        print "Starting Deployable %s" % self.name
        self.generate_config()
        if self.cpe.deployable_start(self.name, self.name) != 0:
            print "*** deployable_start FAILED!!"

    def stop(self):
        if self.cpe.deployable_stop(self.name, self.name) != 0:
            print "*** deployable_stop FAILED!!"

    def reload(self):
        if self.cpe.deployable_reload(self.name, self.name) != 0:
            print "*** deployable_reload FAILED!!"

    def status(self):
        print '*** Deployable.status method not implemented'



