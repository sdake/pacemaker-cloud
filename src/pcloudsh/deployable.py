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
import logging
import libxml2
import exceptions
from pcloudsh import cpe
from pcloudsh import assembly
from pcloudsh import pcmkconfig

class Deployable(object):

    def __init__(self):
        self.conf = pcmkconfig.Config()
        self.xml_file = '%s/db_deployable.xml' % (self.conf.dbdir)

        if os.access(self.xml_file, os.R_OK):
            self.doc = libxml2.parseFile(self.xml_file)
            self.doc_images = self.doc.getRootElement()
        else:
            self.doc = libxml2.newDoc("1.0")
            self.doc.newChild(None, "deployables", None)
            self.doc_images = self.doc.getRootElement()
            self.doc_images.setProp('pcmkc-version', self.conf.version)

        self.cpe = cpe.Cpe()
        self.libvirt_conn = None

    def save(self):
        self.doc.saveFormatFile(self.xml_file, format=1)

    def create(self, name):
        if self.exists(name):
            print '*** Deployable %s already exist' % (name)
            return
        deployable_path = self.doc_images.newChild(None, "deployable", None)
        deployable_path.newProp("name", name)
        self.save()

    def assembly_add(self, dname, aname):
        if not self.exists(dname):
            print '*** Deployable %s does not exist' % (dname)
            return

        fac = assembly.AssemblyFactory()
        if not fac.exists(aname):
            print '*** Assembly %s does not exist' % (aname)
            return

        q = "/deployables/deployable[@name='%s']/assembly[@name='%s']" % (dname, aname)
        test_path = self.doc.xpathEval(q)
        if len(test_path) > 0:
            print '*** Assembly %s is already in Deployable %s' % (aname, dname)
            return

        deployable_path = self.doc.xpathEval("/deployables/deployable[@name='%s']" % dname)
        root_node = deployable_path[0]
        assembly_root = root_node.newChild(None, "assembly", None)
        assembly_root.newProp("name", aname)
        self.save()

    def assembly_remove(self, dname, aname):
        if not self.exists(dname):
            print '*** Deployable %s does not exist' % (dname)
            return

        fac = assembly.AssemblyFactory()
        if not fac.exists(aname):
            print '*** Assembly %s does not exist' % (aname)
            return

        q = "/deployables/deployable[@name='%s']/assembly[@name='%s']" % (dname, aname)
        test_path = self.doc.xpathEval(q)
        if len(test_path) is 0:
            print '*** Assembly %s is not in Deployable %s' % (aname, dname)
            return

        deployable_path = self.doc.xpathEval("/deployables/deployable/assembly[@name='%s']" % aname)
        root_node = deployable_path[0]
        root_node.unlinkNode()
        self.save()

    def generate_config(self, name):

        fac = assembly.AssemblyFactory()

        doc = libxml2.newDoc("1.0")
        dep = doc.newChild(None, "deployable", None)
        dep.setProp("name", name)
        dep.setProp("uuid", name) # TODO
        n_asses = dep.newChild(None, "assemblies", None)
        constraints = dep.newChild(None, 'constraints', None)

        a_list = self.doc.xpathEval("/deployables/deployable[@name='%s']/assembly" % name)
        for a_data in a_list:
            a = fac.get(a_data.prop('name'))

            n_ass = n_asses.newChild(None, 'assembly', None)
            n_ass.setProp("name", a.name)
            n_ass.setProp("uuid", a.uuid)
            n_servs = n_ass.newChild(None, "services", None)

            for r in a.resources_get():
                n_srv = n_servs.newChild(None, 'service', None)
                n_srv.setProp("name", r.type)
                n_srv.setProp("monitor_interval", r.monitor_interval)

        filename = '/var/run/%s.xml' % name
        open(filename, 'w').write(doc.serialize(None, 1))
        doc.freeDoc()


    def start(self, deployable_name):
        if not self.exists(deployable_name):
            print '*** Deployable %s does not exist' % (deployable_name)
            return

        print "Starting Deployable %s" % deployable_name
        self.generate_config(deployable_name)

        if self.cpe.deployable_start(deployable_name, deployable_name) == 0:
            if self.cpe.wait_for_dpe_agent():
                self.cpe.deployable_load(deployable_name, deployable_name)
            else:
                print "*** Given up waiting for dped to start"
        else:
            print "deployable_start FAILED!!"


    def stop(self, deployable_name):
        if not self.exists(deployable_name):
            print '*** Deployable %s does not exist' % (deployable_name)
            return
        if self.cpe.deployable_stop(deployable_name, deployable_name) != 0:
            print "deployable_stop FAILED!!"

    def list(self, listiter):
        deployable_list = self.doc.xpathEval("/deployables/deployable")
        for deployable_data in deployable_list:
            listiter.append("%s" % (deployable_data.prop('name')))

    def exists(self, name):
        dlist = self.doc.xpathEval("/deployables/deployable")
        for d in dlist:
            if name == d.prop('name'):
                return True
        return False

    def status(self, name):
        if not self.exists(deployable_name):
            print '*** Deployable %s does not exist' % (deployable_name)
            return
        if self.libvirt_conn is None:
            self.libvirt_conn = libvirt.open("qemu:///system")
        name_list = self.doc.xpathEval("/deployables/deployable[@name='%s']/assembly" % name)
        print ' %-12s %-12s' % ('Assembly', 'Status')
        print '------------------------'
        for a in name_list:
            name = a.prop('name')
            try:
                ass = self.libvirt_conn.lookupByName(name)
                if ass.isActive():
                    print " %-12s %-12s" % (name, 'Running')
                else:
                    print " %-12s %-12s" % (name, 'Stopped')
            except:
                print " %-12s %-12s" % (name, 'Undefined')

