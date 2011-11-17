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
import libvirt
import re
import random
import logging
import libxml2
import shutil
import exceptions
from pwd import getpwnam

from nova import exception
from nova import flags
from nova import utils
from nova.auth import manager

from pcloudsh import cpe
from pcloudsh import assembly
from pcloudsh import pcmkconfig
from pcloudsh import db_helper


FLAGS = flags.FLAGS


class DeployableDb(object):

    def __init__(self, factory, name):
        self.conf = pcmkconfig.Config()
        self.factory = factory

        query = factory.doc.xpathEval("/%s/%s[@name='%s']" % (self.factory.plural,
            self.factory.singular, name))

        if (len(query)):
            self.xml_node = query[0]
            self.name = self.xml_node.prop("name")
            self.username = self.xml_node.prop("username")
        else:
            self.xml_node = None
            self.name = name
            if self.username == None:
                self.username = 'root'

    def save(self):
        if self.xml_node is None:
            node = self.factory.root_get().newChild(None, self.factory.singular, None)
            node.newProp("name", self.name)
            node.newProp("infrastructure", self.infrastructure)
            node.newProp("username", self.username)
            self.xml_node = node
        else:
            self.xml_node.setProp('name', self.name)
            self.xml_node.setProp('infrastructure', self.infrastructure)
            self.xml_node.setProp('username', self.username)

        self.factory.save()

    def assembly_add(self, aname):
        fac = assembly.AssemblyFactory()
        if not fac.exists(aname):
            print '*** Assembly %s does not exist' % (aname)
            return

        ass = fac.get(aname)
        ass.deployment = self.name
        ass.save()

        self.save()
        if self.xml_node.children != None:
            for c in self.xml_node.children:
                if c.hasProp('name') and c.prop('name') == aname:
                    print '*** Assembly %s is already in Deployable %s' % (aname, self.name)
                    return

        assembly_root = self.xml_node.newChild(None, "assembly", None)
        assembly_root.newProp("name", aname)
        self.save()

    def assembly_remove(self, aname):

        fac = assembly.AssemblyFactory()
        if not fac.exists(aname):
            print '*** Assembly %s does not exist' % (aname)
            return

        self.save()
        if self.xml_node.children != None:
            for c in self.xml_node.children:
                if c.hasProp('name') and c.prop('name') == aname:
                    c.unlinkNode()
                    self.save()
                    return

        print '*** Assembly %s is not in Deployable %s' % (aname, self.name)


    def assembly_list_get(self):
        al = []
        if self.xml_node.children != None:
            for c in self.xml_node.children:
                if c.hasProp('name'):
                    al.append(c.prop('name'))
        return al


    def generate_config(self):

        fac = assembly.AssemblyFactory()

        doc = libxml2.newDoc("1.0")
        dep = doc.newChild(None, "deployable", None)
        dep.setProp("name", self.name)
        dep.setProp("uuid", self.name) # TODO
        n_asses = dep.newChild(None, "assemblies", None)
        constraints = dep.newChild(None, 'constraints', None)

        a_list = self.factory.doc.xpathEval("/deployables/deployable[@name='%s']/assembly" % self.name)
        for a_data in a_list:
            a = fac.get(a_data.prop('name'))

            n_ass = n_asses.newChild(None, 'assembly', None)
            n_ass.setProp("name", a.name)
            n_ass.setProp("uuid", a.uuid)
            n_servs = n_ass.newChild(None, "services", None)

            for r in a.resources_get():
                n_srv = n_servs.newChild(None, 'service', None)
                n_srv.setProp("name", r.name)
                n_srv.setProp("provider", r.provider)
                n_srv.setProp("class", r.klass)
                n_srv.setProp("type", r.type)
                n_srv.setProp("monitor_interval", r.monitor_interval)

                if len(r.params) > 0:
                    n_ps = n_srv.newChild(None, 'paramaters', None)
                    for p in r.params.keys():
                        n_p = n_ps.newChild(None, 'paramater', None)
                        n_p.setProp("name", p)
                        n_p.setProp("value", r.params[p])

        filename = '/var/run/%s.xml' % self.name
        open(filename, 'w').write(doc.serialize(None, 1))
        doc.freeDoc()


class Deployable(DeployableDb):

    def __init__(self, factory, name):
        DeployableDb.__init__(self, factory, name)
        self.cpe = cpe.Cpe()

    def delete(self):
        pass

    def create(self):
        return True

    def start(self):
        print "Starting Deployable %s" % self.name
        self.generate_config()
        if self.cpe.deployable_start(self.name, self.name) != 0:
            print "*** deployable_start FAILED!!"

    def stop(self):
        if self.cpe.deployable_stop(self.name, self.name) != 0:
            print "*** deployable_stop FAILED!!"

    def reload(self):
        if self.cpe.deployable_reload(self.name, self.name) != 0:
            print "*** deployable_reload FAILED!!"

    def status(self):
        print '*** Deployable.status method not implemented'


class OpenstackDeployable(Deployable):
    def __init__(self, factory, name, username):
        self.infrastructure = 'openstack'
        self.username = username

        Deployable.__init__(self, factory, name)

        # TODO flagfile
        FLAGS.state_path = '/var/lib/nova'
        FLAGS.lock_path = '/var/lib/nova/tmp'
        FLAGS.credentials_template = '/usr/share/nova/novarc.template'
        self.nova_manager = manager.AuthManager()
        self.conf.load_novarc(name)

    def create(self):
        uid = 0
        gid = 0
        try:
            user_info = getpwnam(self.username)
            uid = user_info[2]
            gid = user_info[3]
        except KeyError as ex:
            print ex
            return False

        proj_exists = True
        try:
            projs = self.nova_manager.get_projects(self.username)
            if not self.name in projs:
                proj_exists = False
        except:
            proj_exists = False

        try:
            if not proj_exists:
                self.nova_manager.create_project(self.name, self.username,
                        'Project %s created by pcloudsh' % (self.name))
        except (exception.UserNotFound, exception.ProjectExists) as ex:
            print ex
            return False

        os.mkdir(os.path.join(self.conf.dbdir, self.name))
        zipfilename = os.path.join(self.conf.dbdir, self.name, 'nova.zip')
        try:
            zip_data = self.nova_manager.get_credentials(self.username, self.name)
            with open(zipfilename, 'w') as f:
                f.write(zip_data)
        except (exception.UserNotFound, exception.ProjectNotFound) as ex:
            print ex
            return False
        except db.api.NoMoreNetworks:
            print ('*** No more networks available. If this is a new '
                    'installation, you need\nto call something like this:\n\n'
                    '  nova-manage network create pvt 10.0.0.0/8 10 64\n\n')
            return False
        except exception.ProcessExecutionError, e:
            print e
            print ("*** The above error may show that the certificate db has "
                    "not been created.\nPlease create a database by running "
                    "a nova-api server on this host.")
            return False

        os.chmod(zipfilename, 0600)
        os.chown(zipfilename, uid, gid)
        novacreds = os.path.join(self.conf.dbdir, self.name, 'novacreds')
        os.mkdir(novacreds)
        os.system('unzip %s -d %s' % (zipfilename, novacreds))
        os.system('ssh-keygen -f %s' % os.path.join(novacreds, 'nova_key'))

        cwd = os.getcwd()
        os.chdir(novacreds)
        os.system('euca-add-keypair nova_key > nova_key.priv')
        os.chdir(cwd)

        for fn in os.listdir(novacreds):
            if 'nova' in fn:
                os.chown(os.path.join(novacreds, fn), uid, gid)
                os.chmod(os.path.join(novacreds, fn), 0600)

        self.conf.load_novarc(self.name)
        return True
        
    def delete(self):
        if os.access(os.path.join(self.conf.dbdir, self.name), os.R_OK):
            shutil.rmtree(os.path.join(self.conf.dbdir, self.name))
            print ' deleted nova project key and environment'
        try:
            self.nova_manager.delete_project(self.name)
            print ' deleted nova project'
        except exception.ProjectNotFound as ex:
            print ex

    def assembly_add(self, aname):
        Deployable.assembly_add(self, aname)

        fac = assembly.AssemblyFactory()
        fac.register_with_openstack(aname, self.username)

class AeolusDeployable(Deployable):
    def __init__(self, factory, name, username):
        self.infrastructure = 'aeolus'
        self.username = username
        Deployable.__init__(self, factory, name)
        self.libvirt_conn = None

    def status(self):
        if not self.exists(self.name):
            print '*** Deployable %s does not exist' % (self.name)
            return

        if self.libvirt_conn is None:
            self.libvirt_conn = libvirt.open("qemu:///system")

        name_list = self.factory.doc.xpathEval("/deployables/deployable[@name='%s']/assembly" % self.name)
        print ' %-12s %-12s' % ('Assembly', 'Status')
        print '------------------------'
        for a in name_list:
            an = a.prop('name')
            try:
                ass = self.libvirt_conn.lookupByName(an)
                if ass.isActive():
                    print " %-12s %-12s" % (an, 'Running')
                else:
                    print " %-12s %-12s" % (an, 'Stopped')
            except:
                print " %-12s %-12s" % (an, 'Undefined')


class DeployableFactory(db_helper.DbFactory):

    def __init__(self):
        db_helper.DbFactory.__init__(self, 'db_deployable.xml', 'deployables', 'deployable')

    def get(self, name, infrastructure='aeolus', username='root'):
        if name in self.all:
            return self.all[name]

        if infrastructure == 'openstack':
            self.all[name] = OpenstackDeployable(self, name, username)
        else:
            self.all[name] = AeolusDeployable(self, name, username)
        return self.all[name]

    def load(self):
        list = self.doc.xpathEval("/%s/%s" % (self.plural, self.singular))
        for node in list:
            n = node.prop('name')
            if n not in self.all:
                if node.prop('infrastructure') == 'openstack':
                    self.all[n] = OpenstackDeployable(self, n, node.prop('username'))
                else:
                    self.all[n] = AeolusDeployable(self, n, 'root')

    def delete(self, name):
        if name in self.all:
            self.all[name].delete()
        self.delete_instance(name)

    def create(self, name, infrastructure, username):
        d = self.get(name, infrastructure, username)
        if d.create():
            d.save()
        else:
            d.delete()
            self.delete_instance(name)
