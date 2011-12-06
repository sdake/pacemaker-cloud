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
import libxml2
import exceptions

from pcloudsh import deployable
from pcloudsh import pcmkconfig
from pcloudsh import db_helper

have_mod = {'openstack': False, 'libvirt': False, 'aeolus': False, 'ovirt': False}
try:
    from pcloudsh import openstack_deployable
    have_mod['openstack'] = True
except:
    pass
try:
    from pcloudsh import libvirt_deployable
    have_mod['libvirt'] = True
except:
    pass
#try:
#    from pcloudsh import aeolus_deployable
#    have_mod['aeolus'] = True
#except:
#    pass
#try:
#    from pcloudsh import ovirt_deployable
#    have_mod['ovirt'] = True
#except:
#    pass

class DeployableFactory(db_helper.DbFactory):

    def __init__(self, logger):
        self.l = logger
        db_helper.DbFactory.__init__(self, 'db_deployable.xml', 'deployables', 'deployable')

    def create_instance(self, name, infrastructure, username):
        i = None

        if not have_mod[infrastructure]:
            self.l.error('cant load deployable %s (%s module missing)' % (name, infrastructure))
            return None

        if infrastructure == 'libvirt':
            i = libvirt_deployable.LibVirtDeployable(self, name, username)
        elif infrastructure == 'openstack':
            i = openstack_deployable.OpenstackDeployable(self, name, username)
        else:
            self.l.error('cant load deployable %s (module %s not supported)' % (name, infrastructure))

        return i

    def get(self, name, infrastructure='libvirt', username='root'):
        if name in self.all:
            return self.all[name]

        self.all[name] = self.create_instance(name, infrastructure, username)
        return self.all[name]

    def load(self):
        list = self.doc.xpathEval("/%s/%s" % (self.plural, self.singular))
        for node in list:
            n = node.prop('name')
            i = node.prop('infrastructure')
            if i == None:
                i = 'libvirt'
            u = node.prop('username')
            if u == None:
                u = 'nobody'
            if n not in self.all:
                self.all[n] = self.create_instance(n, i, u)

    def delete(self, name):
        if name in self.all:
            self.all[name].delete()
        self.delete_instance(name)

    def create(self, name, infrastructure, username, monitor):
        d = self.get(name, infrastructure, username)
        d.monitor = monitor
        if d.create():
            d.save()
        else:
            d.delete()
            self.delete_instance(name)
