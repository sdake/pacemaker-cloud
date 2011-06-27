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
import jeos
import shutil
import uuid
import guestfs
import fileinput
import ifconfig


class Assembly(object):

    def __init__(self):
        try:
            self.doc = libxml2.parseFile('db_assemblies.xml')
            self.doc_assemblies = self.doc.getRootElement()
        except:
            self.doc = libxml2.newDoc("1.0")
            self.doc.newChild(None, "assemblies", None);
            self.doc_assemblies = self.doc.getRootElement()

    def clone_network_setup(self, g, macaddr, hostname, qpid_broker_ip):
        # change macaddr
        # --------------
        tmp_filename = '/tmp/ifcfg-eth0-%s' % macaddr
        g.download('/etc/sysconfig/network-scripts/ifcfg-eth0', tmp_filename)
        for line in fileinput.FileInput(tmp_filename, inplace=1):
            if 'HWADDR' in line:
               print 'HWADDR="%s"' % macaddr
            else:
               print line
        g.upload(tmp_filename, '/etc/sysconfig/network-scripts/ifcfg-eth0')
        os.unlink(tmp_filename)

        # change hostname
        # --------------
        tmp_filename = '/tmp/network-%s' % macaddr
        g.download('/etc/sysconfig/network', tmp_filename)
        for line in fileinput.FileInput(tmp_filename, inplace=1):
            if 'HOSTNAME' in line:
               print 'HOSTNAME="%s"' % hostname
            else:
               print line
        g.upload(tmp_filename, '/etc/sysconfig/network')
        os.unlink(tmp_filename)

        # change matahari broker
        # ----------------------
        tmp_filename = '/tmp/matahari-%s' % macaddr
        g.download('/etc/sysconfig/matahari', tmp_filename)
        for line in fileinput.FileInput(tmp_filename, inplace=1):
            if 'MATAHARI_BROKER' in line:
               print 'MATAHARI_BROKER="%s"' % qpid_broker_ip
            else:
               print line
        g.upload(tmp_filename, '/etc/sysconfig/matahari')
        os.unlink(tmp_filename)

        # how to write this newline back to the file
        try:
            g.rm('/etc/udev/rules.d/70-persistent-net.rules')
        except:
            pass

    def clone_internal(self, dest, source, source_jeos):
        print "source = %s.xml" % source
        self.dest_doc = libxml2.parseFile("%s.xml" % source)

        source_xml = self.dest_doc.xpathEval('/domain/devices/disk/source')
        source_disk_name = source_xml[0].prop('file')
        dest_disk_name = '/var/lib/libvirt/images/%s.dsk' % dest
        if os.access(dest_disk_name, os.R_OK):
            print '*** assembly %s already exists, delete first.' % (dest_disk_name)
            return
        print 'Copying source %s to destination %s' % (source_disk_name, dest_disk_name)
        shutil.copy2(source_disk_name, dest_disk_name)
        source_xml = self.dest_doc.xpathEval('/domain/name')
        source_xml[0].setContent(dest)
        source_xml = self.dest_doc.xpathEval('/domain/devices/disk/source')
        source_xml[0].setProp('file', dest_disk_name)
        mac = [0x52, 0x54, 0x00, random.randint(0x00, 0xff),
               random.randint(0x00, 0xff), random.randint(0x00, 0xff)]
        macaddr = ':'.join(map(lambda x:"%02x" % x, mac))
        source_xml = self.dest_doc.xpathEval('/domain/devices/interface/mac')
        source_xml[0].setProp('address', macaddr)
        self.uuid = uuid.uuid4()
        source_xml = self.dest_doc.xpathEval('/domain/uuid')
        source_xml[0].setContent(self.uuid.get_hex())

        source_xml = self.dest_doc.xpathEval('/domain/devices/interface/source')
        host_iface = source_xml[0].prop('bridge')
        iface_info = ifconfig.Ifconfig(host_iface)

        g = guestfs.GuestFS()
        g.add_drive_opts(dest_disk_name, format='raw', readonly=0)
        g.launch()
        roots = g.inspect_os()
        for root in roots:
            mps = g.inspect_get_mountpoints(root)
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
                    g.mount(mp_dev[1], mp_dev[0])
                except RuntimeError as msg:
                    print "%s (ignored)" % msg

        self.clone_network_setup(g, macaddr, dest, iface_info.addr_get())

        g.umount_all()
        g.sync()
        del g

        self.dest_doc.saveFormatFile("%s.xml" % dest, format=1)
        os.system("oz-customize -d3 %s-assembly.tdl %s.xml" % (source_jeos, dest))

        assemblies_path = self.doc_assemblies.newChild(None, "assembly", None);
        assemblies_path.newProp("name", dest);
        self.doc.serialize(None, 1)
        self.doc.saveFormatFile('db_assemblies.xml', format=1);

    def clone(self, dest, source, source_jeos):
        self.clone_internal(dest, source, "%s-jeos" % source_jeos);

    def create(self, dest, source):
        self.clone_internal(dest, "%s-jeos" % source, "%s-jeos" % source);

    def list(self, listiter):
        assembly_list = self.doc.xpathEval("/assemblies/assembly")
        for assembly_data in assembly_list:
            listiter.append("%s" % assembly_data.prop('name'));

    def delete(self, name):
        assembly_path = self.doc.xpathEval("/assemblies/assembly[@name='%s']" % name)
        root_node = assembly_path[0]
        root_node.unlinkNode();
        self.doc.saveFile('db_assemblies.xml');
