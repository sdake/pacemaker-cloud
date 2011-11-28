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

from pcloudsh import deployable

class LibVirtDeployable(deployable.Deployable):
    def __init__(self, factory, name, username):
        self.infrastructure = 'libvirt'
        self.username = username
        deployable.Deployable.__init__(self, factory, name)

    def status(self):
        try:
            c = libvirt.open("qemu:///system")
        except:
            self.l.exception('*** couldn\'t connect to libvirt')

        name_list = self.factory.doc.xpathEval("/deployables/deployable[@name='%s']/assembly" % self.name)
        print ' %-12s %-12s' % ('Assembly', 'Status')
        print '------------------------'
        for a in name_list:
            an = a.prop('name')
            try:
                ass = c.lookupByName(an)
                if ass.isActive():
                    print " %-12s %-12s" % (an, 'Running')
                else:
                    print " %-12s %-12s" % (an, 'Stopped')
            except libvirt.libvirtError as e:
                if e.get_error_code() == libvirt.VIR_ERR_NO_DOMAIN:
                    print " %-12s %-12s" % (an, 'Undefined')
                else:
                    print " %-12s %-12s" % (an, 'Error')
        try:
            c.close()
        except:
            self.l.exception('*** couldn\'t disconnect from libvirt')


