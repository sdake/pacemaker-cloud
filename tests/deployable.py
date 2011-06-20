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
        self.dpe_obj = None
        self.conn = cqpid.Connection('localhost:49000')
        self.conn.open()
        self.session = qmf2.ConsoleSession(self.conn)
        self.session.setAgentFilter('[]')
        self.session.open()
        self.l = logging.getLogger()
        time.sleep(3)

        agents = self.session.getAgents()
        for a in agents:
            self.l.debug('agent: %s' % str(a))
            if 'cloudpolicyengine.org' in a.getVendor():
                    result = a.query("{class:cpe, package:'org.cloudpolicyengine'}")
                    if len(result) >= 1:
                        self.cpe_obj = result[0]

        if self.cpe_obj is None:
            print ''
            print 'No cpe agent!, aaarrggg'
            print ''
            sys.exit(3)

    def wait_for_dpe_agent(self, timeout=6):
        waited = 0
        self.dpe_obj = None
        while self.dpe_obj is None and waited < timeout:
            time.sleep(1)
            agents = self.session.getAgents()
            for a in agents:
                if 'cloudpolicyengine.org' in a.getVendor():
                    result = a.query("{class:dpe, package:'org.cloudpolicyengine'}")
                    if len(result) >= 1:
                        self.dpe_obj = result[0]
                        return True
        return False

    def __del__(self):
      self.session.close()
      self.conn.close()

    def deployable_start(self, name, uuid):
        result = None
        if self.cpe_obj:
            try:
                result = self.cpe_obj.deployable_start(name, uuid)
            except:
                return 1

            for k,v in result.items():
                return v
        else:
            return 1

    def deployable_load(self, name, uuid):
        result = None
        if self.dpe_obj:
            try:
                result = self.dpe_obj.deployable_load(name, uuid)
            except:
                return 1

            for k,v in result.items():
                return v
        else:
            return 1

    def deployable_stop(self, name, uuid):
        result = None
        if self.cpe_obj:
            try:
                result = self.cpe_obj.deployable_stop(name, uuid)
            except:
                return 1

            for k,v in result.items():
                print "Output Parameters: %s=%s" % (k, v)
        else:
            return 1

class Service(object):

    def __init__(self, name):
        self.name = name

class Deployable(object):

    def __init__(self, name):
        self.name = name
        self.uuid = name # TODO
        self.assemblies = {}
        self.cpe = Cpe()
        self.l = logging.getLogger()

    def __del__(self):
        self.stop()
        del self.cpe

    def assembly_add(self, ass):
        self.assemblies[ass.name] = ass

    def generate_config(self):
        doc = libxml2.newDoc("1.0")
        dep = doc.newChild(None, "deployable", None)
        dep.setProp("name", self.name)
        dep.setProp("uuid", self.uuid)
        asses = dep.newChild(None, "assemblies", None)
        constraints = dep.newChild(None, 'constraints', None)

        for n, a in self.assemblies.iteritems():
            ass = asses.newChild(None, 'assembly', None)
            ass.setProp("name", n)
            ass.setProp("uuid", n) # TODO
            ass.setProp("ipaddr", a.ipaddr_get())
            a.insert_service_config(ass)

        open(self.name + '.xml', 'w').write(doc.serialize(None, 1))
        doc.freeDoc()

    def start(self, runonly=False):
        self.l.info('starting Deployable %s' % self.name)
        for n, a in self.assemblies.iteritems():
             a.start()

        if runonly:
            return

        self.generate_config()

        if self.cpe.deployable_start(self.name, self.uuid) == 0:
            if self.cpe.wait_for_dpe_agent():
                self.cpe.deployable_load(self.name, self.uuid)
            else:
                self.l.error("given up waiting for dpe to start")
        else:
            l.error("deployable_start FAILED!!")

    def stop(self):
        # send cpe a qmf message saying this deployment is about to
        # be stopped

        for n, a in self.assemblies.iteritems():
            a.stop()

        time.sleep(10)
        self.cpe.deployable_stop(self.name, self.uuid)

