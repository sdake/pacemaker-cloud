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
import logging
import libxml2
import exceptions
import uuid
import subprocess
import shutil
from pwd import getpwnam

from glance import client as glance_client
from glance.common import exception
from glance.common import utils

from nova import exception
from nova import flags
from nova import utils
from nova.auth import manager

from pcloudsh import pcmkconfig
from pcloudsh import deployable
from pcloudsh import assembly
from pcloudsh import assembly_factory

FLAGS = flags.FLAGS

class OpenstackDeployable(deployable.Deployable):
    def __init__(self, factory, name, username):
        self.infrastructure = 'openstack'
        self.username = username

        deployable.Deployable.__init__(self, factory, name)

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


