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
import subprocess

import libvirt

from glance import client as glance_client
from glance.common import exception
from glance.common import utils

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
        return '%s (%s)' % (self.name, self.uuid)

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

class OpenstackAssembly(Assembly):
    def __init__(self, factory, name):
        Assembly.__init__(self, factory, name)
        os.system("modprobe nbd")

        self.keydir = os.path.join(self.conf.dbdir, self.deployment, 'novacreds')
        self.keyfile = 'nova_key'

    def start(self):
        self.l.info('starting openstack:%s:%s' % (self.deployment, self.name))
        cmd = 'su -c \". ./novarc && euca-run-instances %s -k nova_key\" %s' % (self.name, self.username)
        self.l.info('cmd: %s' % (str(cmd)))
        try:
            out = subprocess.check_output(cmd, shell=True, cwd=self.keydir)
            self.l.info('cmd out: %s' % (str(out)))
        except:
            self.l.exception('*** couldn\'t start %s' % self.name)

    def image_to_instance(self, image_name):

        cmd = 'su -c \". ./novarc && euca-describe-images\" %s' % self.username
        output = subprocess.check_output(cmd, shell=True, cwd=self.keydir)

        image_name_search = ' (%s)' % image_name
        ami = None
        for line in output:
            if line != None and image_name_search in line:
                ami_s = line.split()
                if len(ami_s) > 1:
                    ami = ami_s[1]

        if ami is None:
            print 'ami not found'
            return None

        cmd = 'su -c \". ./novarc && euca-describe-instances\" %s' % self.username
        output = subprocess.check_output(cmd, shell=True, cwd=self.keydir)
        inst = None
        for line in output:
            if line != None and ami in line:
                inst_s = line.split()
                if len(inst_s) > 0:
                    inst = inst_s[1]
                    self.l.info('%s => [ami:%s, instance: %s]' % (image_name, ami, inst))
                    return inst
        return None

    def stop(self):
        self.l.info('stopping %s:%s' % (self.deployment, self.name))

        inst = self.image_to_instance(self.name)
        if inst != None:
            try:
                p1 = subprocess.Popen('su -c \". ./novarc && euca-terminate-instances %s\" %s' % (inst, self.username),
                    shell=True, cwd=self.keydir,
                    stderr=subprocess.PIPE, stdout=subprocess.PIPE)
                out = p1.communicate()
            except:
                self.l.exception('*** couldn\'t stop %s' % self.name)

    def status(self):
        return 'Unknown (not impl. yet)'

    def register_with_openstack(self, username):
        print ' Registering assembly image with nova ...'

        creds = dict(username=os.getenv('OS_AUTH_USER'),
                password=os.getenv('OS_AUTH_KEY'),
                tenant=os.getenv('OS_AUTH_TENANT'),
                auth_url=os.getenv('OS_AUTH_URL'),
                strategy=os.getenv('OS_AUTH_STRATEGY', 'noauth'))

        c = glance_client.Client(host="0.0.0.0", port=9292,
                use_ssl=False, auth_tok=None,
                creds=creds)

        parameters = {
            "filters": {},
            "limit": 10,
        }

        image_meta = {'name': self.name,
                      'is_public': True,
                      'disk_format': 'raw',
                      'min_disk': 0,
                      'min_ram': 0,
                      'location': 'file://%s' % (self.image),
                      'owner': self.username,
                      'container_format': 'ovf'}

        images = c.get_images(**parameters)
        for image in images:
            if image['name'] == self.name:
                print ' *** image already in glance: %s > %s' % (image['name'], image['id'])
                return

        try:
            image_meta = c.add_image(image_meta, None)
            image_id = image_meta['id']
            print " Added new image with ID: %s" % image_id
            print " Returned the following metadata for the new image:"
            for k, v in sorted(image_meta.items()):
                print " %(k)30s => %(v)s" % locals()
        except exception.ClientConnectionError, e:
            print (" Failed to connect to the Glance API server."
                   " Is the server running?" % locals())
            pieces = unicode(e).split('\n')
            for piece in pieces:
                print piece
            return
        except Exception, e:
            print " Failed to add image. Got error:"
            pieces = unicode(e).split('\n')
            for piece in pieces:
                print piece
            print (" Note: Your image metadata may still be in the registry, "
                   "but the image's status will likely be 'killed'.")

    def deregister_with_openstack(self):
        print ' deregistering assembly image from glance ...'

        parameters = {
            "filters": {},
            "limit": 10,
        }

        creds = dict(username=os.getenv('OS_AUTH_USER'),
                password=os.getenv('OS_AUTH_KEY'),
                tenant=os.getenv('OS_AUTH_TENANT'),
                auth_url=os.getenv('OS_AUTH_URL'),
                strategy=os.getenv('OS_AUTH_STRATEGY', 'noauth'))

        c = glance_client.Client(host="0.0.0.0", port=9292,
                                use_ssl=False, auth_tok=None,
                                creds=creds)

        images = c.get_images(**parameters)
        image_id = None
        for image in images:
            if image['name'] == self.name:
                image_id = image['id']
                break
        if image_id == None:
            print " *** No image with name %s was found in glance" % self.name
        else:
            try:
                c.delete_image(image_id)
                print " Deleted image %s (%s)" % (self.name, image_id)
            except exception.NotFound:
                print " *** No image with ID %s was found" % image_id
                return
            except exception.NotAuthorized:
                print " *** Glance can't delete the image (%s, %s)" % (image_id, self.name)
                return


class AeolusAssembly(Assembly):

    def __init__(self, factory, name):
        Assembly.__init__(self, factory, name)

    def start(self):
        self.l.info('starting virt:%s:%s' % (self.deployment, self.name))
        libvirt_xml = libxml2.parseFile('/var/lib/pacemaker-cloud/assemblies/%s.xml' % self.name)
        libvirt_doc = libvirt_xml.serialize(None, 1);
        try:
            self.libvirt_conn = libvirt.open("qemu:///system")
        except:
            self.l.exception('*** couldn\'t connect to libvirt')
        try:
            libvirt_dom = self.libvirt_conn.createXML(libvirt_doc, 0)
            self.l.info('started %s' % (self.name))
        except:
            self.l.exception('*** couldn\'t start %s' % self.name)
        try:
            self.libvirt_conn.close()
        except:
            self.l.exception('*** couldn\'t connect to libvirt')

    def stop(self):
        try:
            self.libvirt_conn = libvirt.open("qemu:///system")
        except:
            self.l.exception('*** couldn\'t connect to libvirt')
        try:
            ass = self.libvirt_conn.lookupByName(self.name)
            ass.destroy()
        except:
            self.l.exception('*** couldn\'t stop %s (already stopped?)' % self.name)
        try:
            self.libvirt_conn.close()
        except:
            self.l.exception('*** couldn\'t connect to libvirt')

    def status(self):
        st = 'Unknown'
        try:
            self.libvirt_conn = libvirt.open("qemu:///system")
        except:
            self.l.exception('*** couldn\'t connect to libvirt')
        try:
            ass = self.libvirt_conn.lookupByName(self.name)
            if ass.isActive():
                st = 'Running'
            else:
                st = 'Stopped'
        except:
            st = 'Undefined'
        try:
            self.libvirt_conn.close()
        except:
            self.l.exception('*** couldn\'t connect to libvirt')
        return st


class AssemblyFactory(db_helper.DbFactory):

    def __init__(self, logger):
        self.l = logger
        db_helper.DbFactory.__init__(self, 'db_assemblies.xml', 'assemblies', 'assembly')

    def create_instance(self, name):
        return AeolusAssembly(self, name)

    def load(self):
        list = self.doc.xpathEval("/%s/%s" % (self.plural, self.singular))
        for node in list:
            n = node.prop('name')
            if n not in self.all:
                if node.prop('infrastructure') == 'openstack':
                    self.all[n] = OpenstackAssembly(self, n)
                else:
                    self.all[n] = AeolusAssembly(self, n)

    def clone(self, source, dest):
        source_assy = self.get(source)
        dest_assy = self.get(dest)
        if source_assy.uuid == '':
            print '*** The source assembly does not exist in the system \"%s\"' % source
            return
        dest_assy.clone_from(source_assy)
        dest_assy.clean_xml('%s/assemblies/%s.xml' % (self.conf.dbdir, dest_assy.name))
        dest_assy.save()

    def create(self, name, source):
        dest_assy = self.get(name)
        if not os.access('%s/assemblies/%s.tdl' % (self.conf.dbdir, name), os.R_OK):
            print '*** Please provide %s/assemblies/%s.tdl to customize your assembly' % (self.conf.dbdir, name)
            return

        jeos_source = self.get("%s-jeos" % source);
        jeos_source.jeos_name = source
        if not os.access('%s/jeos/%s-jeos.xml' % (self.conf.dbdir, jeos_source.jeos_name), os.R_OK):
            print '*** Please create the \"%s\" jeos first' % source
            return

        if dest_assy.clone_from(jeos_source) == 0:
            os.system ("oz-customize -d3 %s/assemblies/%s.tdl %s/assemblies/%s.xml" %
                    (self.conf.dbdir, dest_assy.name, self.conf.dbdir, dest_assy.name))
            dest_assy.clean_xml('%s/assemblies/%s.xml' % (self.conf.dbdir, dest_assy.name))
            dest_assy.save()
        else:
            self.delete_instance(name)


    def register_with_openstack(self, name, username):

        # reload as openstack class
        del self.all[name]
        a = OpenstackAssembly(self, name)
        a.username = username
        a.infrastructure = 'openstack'
        a.save()
        a.register_with_openstack(username)
        self.all[name] = a

    def delete(self, name):
        self.get(name).delete()
        self.delete_instance(name)
