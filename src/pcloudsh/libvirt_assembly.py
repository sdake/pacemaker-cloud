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
import logging
import libxml2
import exceptions

import libvirt

from pcloudsh import assembly

class LibVirtAssembly(assembly.Assembly):

    def __init__(self, factory, name):
        assembly.Assembly.__init__(self, factory, name)

    def start(self):
        self.l.info('starting virt:%s:%s' % (self.deployment, self.name))
        libvirt_xml = libxml2.parseFile('/var/lib/pacemaker-cloud/assemblies/%s.xml' % self.name)
        libvirt_doc = libvirt_xml.serialize(None, 1);
        try:
            c = libvirt.open("qemu:///system")
            libvirt_dom = c.createXML(libvirt_doc, 0)
            self.l.info('started %s' % (self.name))
        except libvirt.libvirtError as e:
            self.l.exception(e)
        finally:
            c.close()

    def stop(self):
        try:
            c = libvirt.open("qemu:///system")
            ass = c.lookupByName(self.name)
            ass.destroy()
        except libvirt.libvirtError as e:
            if e.get_error_code() == libvirt.VIR_ERR_NO_DOMAIN:
                # already stopped
                pass
            else:
                self.l.exception(e)
        finally:
            c.close()

    def status(self):
        st = 'Undefined'
        try:
            c = libvirt.open("qemu:///system")
            ass = self.libvirt_conn.lookupByName(self.name)
            if ass.isActive():
                st = 'Running'
            else:
                st = 'Stopped'
        except libvirt.libvirtError as e:
            if e.get_error_code() == libvirt.VIR_ERR_NO_DOMAIN:
                st = 'Stopped'
            else:
                self.l.exception(e)
        finally:
            c.close()

        return st

