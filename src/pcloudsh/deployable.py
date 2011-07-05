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
import cpe
import assembly

class Deployable(object):

    def __init__(self):
        self.xml_file = '/var/lib/pacemaker-cloud/db_deployable.xml'
        try:
            self.doc = libxml2.parseFile(self.xml_file)
            self.doc_images = self.doc.getRootElement()
        except:
            self.doc = libxml2.newDoc("1.0")
            self.doc.newChild(None, "deployables", None);
            self.doc_images = self.doc.getRootElement()
        self.cpe = cpe.Cpe()
        self.libvirt_conn = None

    def save(self):
        self.doc.saveFormatFile(self.xml_file, format=1);

    def create(self, name):
        deployable_path = self.doc_images.newChild(None, "deployable", None);
        deployable_path.newProp("name", name);
        self.save()

    def assembly_add(self, deployable_name, assembly_name):
        deployable_path = self.doc.xpathEval("/deployables/deployable[@name='%s']" % deployable_name)
        root_node = deployable_path[0]
        assembly_root = root_node.newChild(None, "assembly", None);
        assembly_root.newProp("name", assembly_name);
        self.save()

    def assembly_remove(self, deployable_name, assembly_name):
        deployable_path = self.doc.xpathEval("/deployables/deployable/assembly[@name='%s']" % assembly_name)
        root_node = deployable_path[0]
        root_node.unlinkNode();
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

        filename = '/var/lib/pacemaker-cloud/%s.xml' % name
        open(filename, 'w').write(doc.serialize(None, 1))
        doc.freeDoc()


    def start(self, deployable_name):
        if self.libvirt_conn is None:
            self.libvirt_conn = libvirt.open("qemu:///system")

        assembly_list = self.doc.xpathEval("/deployables/deployable[@name='%s']/assembly" % deployable_name)
        print ("Starting Deployable %s" % deployable_name);
        for assembly_data in assembly_list:
            print (" - Starting Assembly %s" % assembly_data.prop('name'))
            libvirt_xml = libxml2.parseFile('/var/lib/pacemaker-cloud/%s.xml' % assembly_data.prop('name'))
            libvirt_doc = libvirt_xml.serialize(None, 1);
            libvirt_dom = self.libvirt_conn.createXML(libvirt_doc, 0)

        self.generate_config(deployable_name)

        if self.cpe.deployable_start(deployable_name, deployable_name) == 0:
            if self.cpe.wait_for_dpe_agent():
                self.cpe.deployable_load(deployable_name, deployable_name)
            else:
                print "*** given up waiting for dpe to start"
        else:
            print "deployable_start FAILED!!"


    def stop(self, deployable_name):
        if self.cpe.deployable_stop(deployable_name, deployable_name) != 0:
            print "deployable_stop FAILED!!"

        if self.libvirt_conn is None:
            self.libvirt_conn = libvirt.open("qemu:///system")
        assembly_list = self.doc.xpathEval("/deployables/deployable[@name='%s']/assembly" % deployable_name)
        print ("Stopping Deployable %s" % deployable_name);
        for assembly_data in assembly_list:
            print (" - Stopping Assembly %s" % assembly_data.prop('name'))

            try:
                ass = self.libvirt_conn.lookupByName(assembly_data.prop('name'))
                ass.destroy()
            except:
                print '*** couldn\'t stop %s (already stopped?)' % assembly_data.prop('name')

    def list(self, listiter):
        deployable_list = self.doc.xpathEval("/deployables/deployable")
        for deployable_data in deployable_list:
            listiter.append("%s" % (deployable_data.prop('name')))

    def status(self, name):
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

