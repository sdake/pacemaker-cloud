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
import time
import re
import random
import logging
import libxml2
import exceptions
import jeos
import shutil
import uuid
import guestfs
import fileinput
import ifconfig
import resource

class Assembly(object):

    def __init__(self, factory):
        self.factory = factory
        self.name = ''
        self.uuid = ''
        self.disk = ''
        self.xml_node = None
        self.rf = None
        self.gfs = None

    def resources_get(self):
        if self.rf == None:
            self.rf = resource.ResourceFactory(self.xml_node)

        return self.rf.all_get()

    def load_from_xml(self, xml):
        self.xml_node = xml

        self.name_set(xml.prop('name'))
        self.uuid = xml.prop('uuid')
        if not os.access(self.disk, os.R_OK):
            print 'warning: assembly %s does not have a disk(%s).' % (self.name, self.disk)
        self.rf = resource.ResourceFactory(xml)

    def save(self):
        if self.xml_node is None:
            ass = self.factory.root_get().newChild(None, "assembly", None)
            ass.newProp("name", self.name)
            ass.newProp("uuid", self.uuid)
            ass.newChild(None, "resources", None);
            self.xml_node = ass
        else:
            self.xml_node.setProp('name', self.name)
            self.xml_node.setProp('uuid', self.uuid)

        self.factory.save()

    def uuid_set(self, uuid):
        self.uuid = uuid

    def name_get(self):
        return self.name

    def uuid_get(self):
        return self.uuid

    def name_set(self, name):
        self.name = name
        self.disk = '/var/lib/libvirt/images/%s.qcow2' % self.name

    def __str__(self):
        return '%s (%s)' % (self.name, self.uuid)

    def clone_network_setup(self, macaddr, qpid_broker_ip):
        # change macaddr
        # --------------
        tmp_filename = '/tmp/ifcfg-eth0-%s' % macaddr
        self.gfs.download('/etc/sysconfig/network-scripts/ifcfg-eth0', tmp_filename)
        for line in fileinput.FileInput(tmp_filename, inplace=1):
            if 'HWADDR' in line:
               print 'HWADDR="%s"' % macaddr
            else:
               print line
        self.gfs.upload(tmp_filename, '/etc/sysconfig/network-scripts/ifcfg-eth0')
        os.unlink(tmp_filename)

        # change hostname
        # ---------------
        tmp_filename = '/tmp/network-%s' % macaddr
        self.gfs.download('/etc/sysconfig/network', tmp_filename)
        for line in fileinput.FileInput(tmp_filename, inplace=1):
            if 'HOSTNAME' in line:
               print 'HOSTNAME="%s"' % self.name
            else:
               print line
        self.gfs.upload(tmp_filename, '/etc/sysconfig/network')
        os.unlink(tmp_filename)

        # change matahari broker
        # ----------------------
        tmp_filename = '/tmp/matahari-%s' % macaddr
        self.gfs.download('/etc/sysconfig/matahari', tmp_filename)
        for line in fileinput.FileInput(tmp_filename, inplace=1):
            if 'MATAHARI_BROKER' in line:
               print 'MATAHARI_BROKER="%s"' % qpid_broker_ip
            else:
               print line
        self.gfs.upload(tmp_filename, '/etc/sysconfig/matahari')
        os.unlink(tmp_filename)

        # how to write this newline back to the file
        try:
            self.gfs.rm('/etc/udev/rules.d/70-persistent-net.rules')
        except:
            pass

    def guest_mount(self):
        if not self.gfs is None:
            return

        self.gfs = guestfs.GuestFS()
        self.gfs.add_drive_opts(self.disk, format='qcow2', readonly=0)
        self.gfs.launch()
        roots = self.gfs.inspect_os()
        for root in roots:
            mps = self.gfs.inspect_get_mountpoints(root)
            def compare(a, b):
                if len(a[0]) > len(b[0]):
                    return 1
                elif len(a[0]) == len(b[0]):
                    return 0
                else:
                    return -1
            mps.sort(compare)
            for mp_dev in mps:
                try:
                    self.gfs.mount(mp_dev[1], mp_dev[0])
                except RuntimeError as msg:
                    print "%s (ignored)" % msg

    def guest_unmount(self):
        if self.gfs is None:
            return
        self.gfs.umount_all()
        self.gfs.sync()
        del self.gfs
        self.gfs = None


    def clone_from(self, source, source_jeos):
        if os.access(self.disk, os.R_OK):
            print '*** assembly %s already exists, delete first.' % (self.disk)
            return -1

        print "source = %s.xml" % source
        self.jeos_doc = libxml2.parseFile("/var/lib/pacemaker-cloud/jeos/%s.xml" % source_jeos)

        source_xml = self.jeos_doc.xpathEval('/domain/devices/disk/source')
        jeos_disk_name = '/var/lib/libvirt/images/%s.qcow2' % source
        print 'Copying %s to %s' % (jeos_disk_name, self.disk)
        shutil.copy2(jeos_disk_name, self.disk)
        source_xml = self.jeos_doc.xpathEval('/domain/name')
        source_xml[0].setContent(self.name)
        source_xml = self.jeos_doc.xpathEval('/domain/devices/disk/source')
        source_xml[0].setProp('file', self.disk)
        mac = [0x52, 0x54, 0x00, random.randint(0x00, 0xff),
               random.randint(0x00, 0xff), random.randint(0x00, 0xff)]
        macaddr = ':'.join(map(lambda x:"%02x" % x, mac))
        source_xml = self.jeos_doc.xpathEval('/domain/devices/interface/mac')
        source_xml[0].setProp('address', macaddr)
        self.uuid = uuid.uuid4().get_hex()
        source_xml = self.jeos_doc.xpathEval('/domain/uuid')
        source_xml[0].setContent(self.uuid)

        source_xml = self.jeos_doc.xpathEval('/domain/devices/interface/source')
        host_iface = source_xml[0].prop('bridge')
        iface_info = ifconfig.Ifconfig(host_iface)

        self.guest_mount()
        self.clone_network_setup(macaddr, iface_info.addr_get())
        self.guest_unmount()

        self.jeos_doc.saveFormatFile("/var/lib/pacemaker-cloud/assemblies/%s.xml" % self.name, format=1)
        os.system("oz-customize -d3 /var/lib/pacemaker-cloud/jeos/%s-assembly.tdl /var/lib/pacemaker-cloud/assemblies/%s.xml" % (source_jeos, self.name))
        self.save()
        return 0

    def resource_add(self, rsc_name, rsc_type):
        '''
        resource_add <resource name> <resource template> <assembly_name>
        '''
        if self.rf == None:
            self.rf = resource.ResourceFactory(self.xml_node)

        r = self.rf.get(rsc_name)
        r.name = rsc_name
        r.type = rsc_type
        r.save()

        self.save()

    def resource_remove(self, rsc_name):
        '''
        resource_remove <resource name> <assembly_name>
        '''
        if self.rf == None:
            self.rf = resource.ResourceFactory(self.xml_node)

        self.rf.delete(rsc_name)
        self.save()

class AssemblyFactory(object):

    def __init__(self):
        self.xml_file = '/var/lib/pacemaker-cloud/db_assemblies.xml'
        it_exists = os.access(self.xml_file, os.R_OK)

        try:
            if it_exists:
                self.doc = libxml2.parseFile(self.xml_file)
        except:
            it_exists = False

        if not it_exists:
            self.doc = libxml2.newDoc("1.0")
            self.doc.newChild(None, "assemblies", None);

        self.root_node = self.doc.getRootElement()
        self.all = {}
        assembly_list = self.doc.xpathEval("/assemblies/assembly")
        for assembly_data in assembly_list:
            n = assembly_data.prop('name')
            if n not in self.all:
                self.all[n] = Assembly(self)
                self.all[n].load_from_xml(assembly_data)

    def root_get(self):
        return self.root_node

    def clone(self, name, source, source_jeos):
        a = self.get(name)
        a.clone_from(source, "%s-jeos" % source_jeos);

    def create(self, name, source):
        if not os.access('/var/lib/pacemaker-cloud/assemblies/%s.tdl' % name, os.R_OK):
            print '*** please provide /var/lib/pacemaker-cloud/assemblies/%s.tdl to customize your assembly' % name
            return

        a = self.get(name)
        if a.clone_from("%s-jeos" % source, "%s-jeos" % source) == 0:
            os.system ("oz-customize -d3 /var/lib/pacemaker-cloud/assemblies/%s.tdl /var/lib/pacemaker-cloud/assemblies/%s.xml" % (name, name))

    def exists(self, name):
        if name in self.all:
            return True
        else:
            return False

    def get(self, name):
        if name in self.all:
            return self.all[name]

        a = Assembly(self)
        a.name_set(name)
        self.all[name] = a
        a.save()
        return a

    def delete(self, name):
        assembly_path = self.doc.xpathEval("/assemblies/assembly[@name='%s']" % name)
        root_node = assembly_path[0]
        root_node.unlinkNode();
        self.save()

    def save(self):
        self.doc.saveFormatFile(self.xml_file, format=1)

    def all_get(self):
        return self.all.values()

    def list(self, listiter):
        for a in self.all:
            listiter.append(str(a));

