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
import random
import logging
import libxml2
import exceptions
import jeos
import shutil
import uuid
import guestfs
import fileinput
import subprocess

from pcloudsh import ifconfig
from pcloudsh import resource
from pcloudsh import pcmkconfig
from pcloudsh import db_helper

class Assembly(object):

    def __init__(self, factory, name):
        self.conf = pcmkconfig.Config()
        self.factory = factory
        self.l = factory.l

        query = factory.doc.xpathEval("/assemblies/assembly[@name='%s']" % name)

        self.rf = None
        self.gfs = None
        self.infrastructure = None
        self.username = None
        self.deployment = None
        if (len(query)):
            self.xml_node = query[0]
            self.name = self.xml_node.prop ("name")
            self.jeos_name = self.xml_node.prop ("jeos_name")
            self.image = self.xml_node.prop ("image")
            self.uuid = self.xml_node.prop ("uuid")
            if self.xml_node.hasProp('infrastructure'):
                self.infrastructure = self.xml_node.prop("infrastructure")
            if self.xml_node.hasProp('username'):
                self.username = self.xml_node.prop("username")
            if self.xml_node.hasProp('deployment'):
                self.deployment = self.xml_node.prop("deployment")
        else:
            self.factory = factory
            self.name = name
            self.image = '/var/lib/libvirt/images/%s.qcow2' % self.name
            self.uuid = ''
            self.jeos_name = ''
            self.xml_node = None

    def save(self):
        if self.xml_node is None:
            ass = self.factory.root_get().newChild(None, "assembly", None)
            ass.newProp("name", self.name)
            ass.newProp("uuid", self.uuid)
            ass.newProp("jeos_name", self.jeos_name)
            ass.newProp("image", self.image)
            ass.newChild(None, "resources", None)
            if self.infrastructure != None:
                ass.newProp("infrastructure", self.infrastructure)
            if self.username != None:
                ass.newProp("username", self.username)
            if self.deployment != None:
                ass.newProp("deployment", self.deployment)
            self.xml_node = ass
        else:
            self.xml_node.setProp('name', self.name)
            self.xml_node.setProp('uuid', self.uuid)
            self.xml_node.setProp('jeos_name', self.jeos_name)
            self.xml_node.setProp('image', self.image)
            if self.infrastructure != None:
                self.xml_node.setProp("infrastructure", self.infrastructure)
            if self.username != None:
                self.xml_node.setProp("username", self.username)
            if self.deployment != None:
                self.xml_node.setProp("deployment", self.deployment)

        self.factory.save()

    def resources_get(self):
        self.resource_factory_setup()
        return self.rf.all_get()

    def resource_factory_setup(self):
        if self.rf == None:
            if self.xml_node is None:
                self.save()
            self.rf = resource.ResourceFactory(self.xml_node)

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
        return '%s (%s) [%s]' % (self.name, self.uuid, self.infrastructure)

    def remove_udev_net(self):
        # remove udev persistent net rule so it is regenerated on reboot
        self.guest_mount()
        try:
            self.gfs.rm('/etc/udev/rules.d/70-persistent-net.rules')
        except:
            pass
        self.guest_unmount()

    def clone_network_setup_debian(self, macaddr, qpid_broker_ip):
        self.guest_mount()
        # change hostname
        # ---------------
        tmp_filename = '/tmp/network-%s' % macaddr
        file = open(tmp_filename, 'w')
        file.write('%s' % self.name)
        file.close()
        self.gfs.upload(tmp_filename, '/etc/hostname')
        os.unlink(tmp_filename)

        # change matahari broker
        # ----------------------
        tmp_filename = '/tmp/matahari-%s' % macaddr
        self.gfs.download('/etc/default/matahari', tmp_filename)
        for line in fileinput.FileInput(tmp_filename, inplace=1):
            if 'MATAHARI_BROKER' in line:
                print 'MATAHARI_BROKER="%s"' % qpid_broker_ip
            else:
                print line
        self.gfs.upload(tmp_filename, '/etc/default/matahari')
        os.unlink(tmp_filename)
        self.guest_unmount()

    def clone_network_setup_fedora(self, macaddr, qpid_broker_ip):
        self.guest_mount()
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
        self.guest_unmount()

    def clone_network_setup(self, jeos_name, macaddr, qpid_broker_ip):
        if jeos_name == "F14-x86_64":
            self.clone_network_setup_fedora(macaddr, qpid_broker_ip)
        if jeos_name == "F15-x86_64":
            self.clone_network_setup_fedora(macaddr, qpid_broker_ip)
        if jeos_name == "F16-x86_64":
            self.clone_network_setup_fedora(macaddr, qpid_broker_ip)
        if jeos_name == "rhel61-x86_64":
            self.clone_network_setup_fedora(macaddr, qpid_broker_ip)
        if jeos_name == "U10-x86_64":
            self.clone_network_setup_debian(macaddr, qpid_broker_ip)

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

    def conf_xml(self, source):
        libvirt_xml = libxml2.parseFile(source)
        devices = libvirt_xml.xpathEval('/domain/devices')
        serial = devices[0].newChild(None, "serial", None)
        serial.setProp("type", "tcp")
        serialSource = serial.newChild(None, "source", None)
        serialSource.setProp("mode", "bind")
        serialSource.setProp("host", "127.0.0.1")
        serialSource.setProp("service", str(random.randrange(1024, 65535)))
        serialProtocol = serial.newChild(None, "protocol", None)
        serialProtocol.setProp("type", "raw")
        serialTarget = serial.newChild(None, "target", None)
        serialTarget.setProp("port", "1")
        libvirt_xml.saveFormatFile(source, format=1)

    def clean_xml(self, source):
        libvirt_xml = libxml2.parseFile(source)
        source_xml = libvirt_xml.xpathEval('/domain/devices/serial')
        root_node = source_xml[0]
        root_node.unlinkNode()
        libvirt_xml.saveFormatFile(source, format=1)

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

        self.jeos_doc.saveFormatFile("%s/assemblies/%s.xml" % (self.conf.dbdir, self.name), format=1)
        self.conf_xml('%s/assemblies/%s.xml' % (self.conf.dbdir, self.name))
        print "jeos assembly %s-assembly.tdl" % source.jeos_name

        # remove the udev net rule before starting the VM
        self.remove_udev_net()

        os.system("oz-customize -d3 %s/jeos/%s-jeos-assembly.tdl %s/assemblies/%s.xml" %
                (self.conf.dbdir, source.jeos_name, self.conf.dbdir, self.name))

        # configure the cloned network configuration
        self.clone_network_setup(source.jeos_name, macaddr, iface_info.addr_get())

        # remove the udev net rule after the VM configuration is completed
        self.remove_udev_net()

        self.jeos_name = source.jeos_name
        self.save()
        return 0

    def delete(self):
        if os.access(self.image, os.R_OK):
            os.unlink(self.image)
            print ' deleted image %s' % self.image

        xml = '%s/assemblies/%s.xml' % (self.conf.dbdir, self.name)
        if os.access(xml, os.R_OK):
            os.unlink(xml)
            print ' deleted virt xml file %s' % xml
        if self.infrastructure == 'openstack':
            self.deregister_with_openstack()

    def resource_add(self, rsc_name, rsc_type):
        '''
        resource_add <resource name> <resource template>
        '''
        self.resource_factory_setup()

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

    def escalation_resource_set(self, rsc_name, escalation_f, escalation_p):
        self.resource_factory_setup()

        if not self.rf.exists(rsc_name):
            print '*** Resource %s does not exist in Assembly %s' % (rsc_name, self.name)
            return

        r = self.rf.get(rsc_name)
        r.escalation_failures = escalation_f
        r.escalation_period = escalation_p
        r.save()
        self.save()

    def resource_remove(self, rsc_name):
        '''
        resource_remove <resource name> <assembly_name>
        '''
        self.resource_factory_setup()

        if not self.rf.exists(rsc_name):
            print '*** Resource %s is not in Assembly %s' % (rsc_name, self.name)
            return

        self.rf.delete(rsc_name)
        self.save()



