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
import assembly
import logging
import cqpid
import qmf2
import libxml2
import sys
import time

class Cpe(object):

    def __init__(self):

        self.cpe_obj = None
        self.conn = cqpid.Connection('localhost:49000')
        self.conn.open()
        self.session = qmf2.ConsoleSession(self.conn)
        #self.session.setAgentFilter('[org.cloudpolicyengine]')
        self.session.setAgentFilter('[]')
        self.session.open()
        time.sleep(3)

        agents = self.session.getAgents()
        for a in agents:
            if 'cloudpolicyengine.org' in a.getVendor() and 'cpe' in a.getProduct():
                self.cpe_obj = a.query("{class:cpe, package:'org.cloudpolicyengine'}")[0]

        if self.cpe_obj is None:
            print ''
            print 'No cpe agent!, aaarrggg'
            print ''
            sys.exit(3)

    def deployable_start(self, name, uuid):
        if self.cpe_obj:
            self.cpe_obj.deployable_start(name, uuid)

    def deployable_stop(self, name, uuid):
        if self.cpe_obj:
            self.cpe_obj.deployable_stop(name, uuid)


class Deployable(object):

    def __init__(self, name):
        self.really_start_guests = False
        self.name = name
        self.uuid = name # TODO
        self.assemblies = {}
        self.cpe = Cpe()

    def __del__(self):
        self.stop()

    def assembly_add(self, ass):
        self.assemblies[ass.name] = ass

    def generate_config(self):
        doc = libxml2.newDoc("1.0")
        cfg = doc.newChild(None, "configuration", None)
        nodes = cfg.newChild(None, "nodes", None)
        resources = cfg.newChild(None, "resources", None)
        constraints = cfg.newChild(None, 'constraints', None)

        for n, a in self.assemblies.iteritems():
            node = nodes.newChild(None, 'node', n)

        self.xmlconfig = doc.serialize(None, 1)
        doc.freeDoc()

    def start(self):
        if self.really_start_guests:
            for n, a in self.assemblies.iteritems():
                 a.start()

        self.generate_config()

        self.cpe.deployable_start(self.name, self.uuid)

    def stop(self):
        # send cpe a qmf message saying this deployment is about to
        # be stopped

        if self.really_start_guests:
            for n, a in self.assemblies.iteritems():
                a.stop()

        self.cpe.deployable_stop(self.name, self.uuid)

