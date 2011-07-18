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
from pcloudsh import ifconfig
from pcloudsh import resource
from pcloudsh import pcmkconfig

class Assembly(object):

    def __init__(self, factory, name):
        self.conf = pcmkconfig.Config()
        self.factory = factory
        self.xml_node = factory.doc.xpathEval("/assemblies/assembly[@name='%s']" % name)
        if (len (self.xml_node)):
            self.name = self.xml_node[0].prop ("name")
            self.jeos_name = self.xml_node[0].prop ("jeos_name")
            self.image = self.xml_node[0].prop ("image")
            self.uuid = self.xml_node[0].prop ("uuid")
            self.rf = None
            self.gfs = None
        else:
            self.factory = factory
            self.name = name
            self.image = '/var/lib/libvirt/images/%s.qcow2' % self.name
            self.uuid = ''
            self.xml_node = None
            self.rf = None
            self.gfs = None

    def resources_get(self):
        if self.rf == None:
            self.rf = resource.ResourceFactory(self.xml_node[0])

        return self.rf.all_get()

    def save(self):
        if self.xml_node is None:
            ass = self.factory.root_get().newChild(None, "assembly", None)
            ass.newProp("name", self.name)
            ass.newProp("uuid", self.uuid)
            ass.newProp("jeos_name", self.jeos_name)
            ass.newProp("image", self.image)
            ass.newChild(None, "resources", None)
            self.xml_node = ass
        else:
            self.xml_node[0].setProp('name', self.name)
            self.xml_node[0].setProp('uuid', self.uuid)
            self.xml_node[0].setProp('jeos_name', self.jeos_name)
            self.xml_node[0].setProp('image', self.image)

        self.factory.save()

    def uuid_set(self, uuid):
        self.uuid = uuid

    def name_get(self):
        return self.name

    def name_jeos_get(self):
        return self.jeos_name

    def uuid_get(self):
        return self.uuid

    def name_set(self, name):
        self.name = name
        self.image = '/var/lib/libvirt/images/%s.qcow2' % self.name

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
        self.gfs.add_drive_opts(self.image, format='qcow2', readonly=0)
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


    def clone_from(self, source):
        if os.access(self.image, os.R_OK):
            print '*** Assembly %s already exists, delete first.' % (self.image)
            return -1

        print "source file is %s-jeos.xml" % source.jeos_name
        self.jeos_doc = libxml2.parseFile("%s/jeos/%s-jeos.xml" % (self.conf.dbdir, source.jeos_name));

        source_xml = self.jeos_doc.xpathEval('/domain/devices/disk/source')
        print 'Copying %s to %s' % (source.image, self.image)
        shutil.copy2(source.image, self.image)
        source_xml = self.jeos_doc.xpathEval('/domain/name')
        source_xml[0].setContent(self.name)
        source_xml = self.jeos_doc.xpathEval('/domain/devices/disk/source')
        source_xml[0].setProp('file', self.image)
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

        self.jeos_doc.saveFormatFile("%s/assemblies/%s.xml" % (self.conf.dbdir, self.name), format=1)
        print "jeos assembly %s-assembly.tdl" % source.jeos_name
        os.system("oz-customize -d3 %s/jeos/%s-jeos-assembly.tdl %s/assemblies/%s.xml" %
                (self.conf.dbdir, self.conf.dbdir, source.jeos_name, self.name))
        self.jeos_name = source.jeos_name
        self.factory.add(self)
        self.save()
        return 0

    def resource_add(self, rsc_name, rsc_type):
        '''
        resource_add <resource name> <resource template>
        '''
        if self.rf == None:
            self.rf = resource.ResourceFactory(self.xml_node[0])

        if self.rf.exists(rsc_name):
            print '*** Resource %s already in Assembly %s' % (rsc_name, self.name)
            return
        if not self.rf.template_exists(rsc_type):
            print '*** Resource template %s does not exist' % (rsc_type)
            return
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

        if not self.rf.exists(rsc_name):
            print '*** Resource %s is not in Assembly %s' % (rsc_name, self.name)
            return

        self.rf.delete(rsc_name)
        self.save()

class AssemblyFactory(object):

    def __init__(self):
        self.conf = pcmkconfig.Config()
        self.xml_file = '%s/db_assemblies.xml' % (self.conf.dbdir)
        it_exists = os.access(self.xml_file, os.R_OK)

        try:
            if it_exists:
                self.doc = libxml2.parseFile(self.xml_file)
        except:
            it_exists = False

        if not it_exists:
            self.doc = libxml2.newDoc("1.0")
            ass_node = self.doc.newChild(None, "assemblies", None)
            ass_node.setProp('pcmkc-version', self.conf.version)

        self.root_node = self.doc.getRootElement()
        _ver = self.root_node.prop('pcmkc-version')
        if not _ver == self.conf.version:
            _msg = '*** warning xml and program version mismatch'
            print '%s \"%s\" != \"%s\"' % (_msg, _ver, self.conf.version)

        self.all = {}
        assembly_list = self.doc.xpathEval("/assemblies/assembly")
        for assembly_data in assembly_list:
            n = assembly_data.prop('name')
            if n not in self.all:
                self.all[n] = Assembly(self, n)

    def root_get(self):
        return self.root_node

    def clone(self, source, dest):
        source_assy = self.get(source)
        dest_assy = self.get(dest)
	if source_assy.uuid == '':
            print '*** The source assembly does not exist in the system \"%s\"' % source
            return
        dest_assy.clone_from(source_assy)

    def create(self, name, source):
        dest_assy = self.get(name)
        if not os.access('%s/assemblies/%s.tdl' % (self.conf.dbdir, name), os.R_OK):
            print '*** Please provide %s/assemblies/%s.tdl to customize your assembly' % (self.conf.dbdir, name)
            return

        jeos_source = self.get("%s-jeos" % source);
        jeos_source.jeos_name = source
        if not os.access('%s/jeos/%s-jeos.tdl' % (self.conf.dbdir, jeos_source.jeos_name), os.R_OK):
            print '*** Please create the \"%s\" jeos first' % source
            return

        if dest_assy.clone_from(jeos_source) == 0:
            os.system ("oz-customize -d3 %s/jeos/%s-jeos.tdl %s/assemblies/%s.xml" % 
                    (self.conf.dbdir, self.conf.dbdir, jeos_source.jeos_name, dest_assy.name))

    def exists(self, name):
        if name in self.all:
            return True
        else:
            return False

    def get(self, name):
        if name in self.all:
            return self.all[name]

        a = Assembly(self, name)
        return a

    def delete(self, name):
        assembly_path = self.doc.xpathEval("/assemblies/assembly[@name='%s']" % name)
        root_node = assembly_path[0]
        root_node.unlinkNode()
        self.save()

    def add(self, assy):
        if assy.name not in self.all:
            self.all[assy.name] = assy

    def save(self):
        self.doc.saveFormatFile(self.xml_file, format=1)

    def all_get(self):
        return self.all.values()

    def list(self, listiter):
        for a in self.all:
            listiter.append(str(a))

