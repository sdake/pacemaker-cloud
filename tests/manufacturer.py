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
import time
import re
import random
import unittest
import logging
import libxml2
import libvirt
import ConfigParser
import oz.TDL
import oz.GuestFactory
import assembly


class Manufacturer(object):

    def __init__(self, dist):
        self.assemblies = {}
        self.dist = dist
        self.l = logging.getLogger()

    def get_conf(self, config, section, key, default):
        if config is not None and config.has_section(section) \
                and config.has_option(section, key):
            return config.get(section, key)
        else:
            return default

    def assemble(self, name, number):
        instname = '%s-%d' % (name, number)
        confdir = name
        image_filename = '/var/lib/libvirt/images/%s.dsk' % instname

        libvirt_filename = '%s/%d.xml' % (name, number)
        tdl_filename = '%s/%d.tdl' % (name, number)
        kickstart_filename = '%s/%d.ks' % (name, number)

        if not os.access(name, os.F_OK):
            os.mkdir(name)

        if not os.access(tdl_filename, os.F_OK):
            # get the tdl and fix the name
            doc = libxml2.parseFile('templates/%s.tdl' % self.dist)
            result = doc.xpathEval('/template/name')
            for node in result:
                node.setContent("%s" % instname)

            tmp_tdl = open(tdl_filename, mode='w+')
            tmp_tdl.write(doc.serialize(format=1))
            doc.freeDoc()
            tmp_tdl.close()

        tdl = oz.TDL.TDL(open(tdl_filename, 'r').read())

        if not os.access(kickstart_filename, os.F_OK):
            net = '--bootproto=dhcp --device=eth0 --onboot=on --hostname=%s' % instname
            o = open(kickstart_filename,"w")
            data = open("templates/%s.ks" % self.dist).read()
            o.write(re.sub("@NETWORK@",net, data))
            o.close()

        oz_config = ConfigParser.SafeConfigParser()
        oz_config.read('/etc/oz/oz.config')

        libvirt_uri = self.get_conf(oz_config, 'libvirt', 'uri',
                                         'qemu:///system')
        libvirt_conn = libvirt.open(libvirt_uri)

        try:
            guest = oz.GuestFactory.guest_factory(tdl, oz_config, kickstart_filename)
        except oz.OzException, exc:
                self.l.error(str(exc))
                raise
    
        guest = oz.GuestFactory.guest_factory(tdl, oz_config, kickstart_filename)
        try:
            guest.check_for_guest_conflict()
            libvirt_xml = guest.jeos(False)
        except:
            libvirt_xml = libxml2.parseFile(libvirt_filename).serialize(None, 1)

        if libvirt_xml is None:
            self.l.info('installing media...')
            guest.generate_install_media(False)
            try:
                self.l.info('generating disk image...')
                guest.generate_diskimage()
                self.l.info('installing fedora onto guest...')
                libvirt_xml = guest.install(50000)
                open(libvirt_filename, 'w').write(libvirt_xml)
            finally:
                guest.cleanup_install()
        else:
            self.l.info('already installed')

        self.assemblies[instname] = assembly.Assembly(name, number, oz_config, tdl)
        return self.assemblies[instname] 

