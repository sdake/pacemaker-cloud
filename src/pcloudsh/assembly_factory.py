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

from pcloudsh import assembly
from pcloudsh import pcmkconfig
from pcloudsh import db_helper

have_mod = {'openstack': False, 'libvirt': False, 'aeolus': False, 'ovirt': False}
try:
    from pcloudsh import openstack_assembly
    have_mod['openstack'] = True
except:
    pass
try:
    from pcloudsh import libvirt_assembly
    have_mod['libvirt'] = True
except:
    pass
#try:
#    from pcloudsh import aeolus_assembly
#    have_mod['aeolus'] = True
#except:
#    pass
#try:
#    from pcloudsh import ovirt_assembly
#    have_mod['ovirt'] = True
#except:
#    pass

class AssemblyFactory(db_helper.DbFactory):

    def __init__(self, logger):
        self.l = logger
        db_helper.DbFactory.__init__(self, 'db_assemblies.xml', 'assemblies', 'assembly')

    def create_instance(self, name, infrastructure=None):
        self.l.debug('creating %s as %s' % (name, infrastructure))
        if infrastructure == None:
            return assembly.Assembly(self, name)
        if not have_mod[infrastructure]:
            self.l.error('cant load assembly %s (%s module missing)' % (name, infrastructure))
            return None
        i = None
        if infrastructure == 'libvirt':
            i = libvirt_assembly.LibVirtAssembly(self, name)
        elif infrastructure == 'openstack':
            i = openstack_assembly.OpenstackAssembly(self, name)
        else:
            self.l.error('cant load assembly %s (module %s not supported)' % (name, infrastructure))
            return None

        i.infrastructure = infrastructure
        return i

    def load(self):
        list = self.doc.xpathEval("/%s/%s" % (self.plural, self.singular))
        for node in list:
            n = node.prop('name')
            i = node.prop('infrastructure')
            if i == None:
                self.l.warn('AssemblyFactory loading %s as libvirt' % n)
                i = 'libvirt'
            if n not in self.all:
                self.all[n] = self.create_instance(n, i)

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

    def register(self, name, infrastructure, dep_name, username):
        if not have_mod[infrastructure]:
            self.l.error('cant register %s as %s module not available' % (name, infrastructure))
            return

        # reload with the correct class
        del self.all[name]
        a = self.create_instance(name, infrastructure)
        a.username = username
        a.deployment = dep_name
        self.all[name] = a
        self.l.debug(str(a))
        a.save()
        if infrastructure == 'openstack':
            a.register_with_openstack(username)

    def delete(self, name):
        self.get(name).delete()
        self.delete_instance(name)

