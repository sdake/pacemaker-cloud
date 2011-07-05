#!/usr/bin/env python
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
import cqpid
from qmf2 import *
import libvirt
import libxml2

class VMLauncher(AgentHandler):
    """
    This agent is implemented as a single class that inherits AgentHandler.
    It does not use a separate thread since once set up, it is driven strictly by
    incoming method calls.
    """

    def __init__(self, url):
        ##
        ## Create and open a messaging connection to a broker.
        ##
        self.connection = cqpid.Connection(url, "{reconnect:True}")
        self.session = None
        self.connection.open()

        ##
        ## Create, configure, and open a QMFv2 agent session using the connection.
        ##
        self.session = AgentSession(self.connection, "{interval:30}")
        self.session.setVendor('pacemakercloud.org')
        self.session.setProduct('vmlauncher')
        self.session.setAttribute('attr1', 1000)
        self.session.open()

        self.libvirt_conn = None

        ##
        ## Initialize the parent class.
        ##
        AgentHandler.__init__(self, self.session)


    def shutdown(self):
        """
        Clean up the session and connection.
        """
        if self.session:
            self.session.close()
        self.connection.close()

    def method(self, handle, methodName, args, subtypes, addr, userId):
        """
        Handle incoming method calls.
        """
        if addr == self.controlAddr:

            if self.libvirt_conn is None:
                self.libvirt_conn = libvirt.open("qemu:///system")

            self.control.methodCount += 1
            name = args['name']
            uuid = args['uuid']
            print 'MSG %s %s %s' % (methodName, name, uuid)
            try:
                if methodName == "start":

                    libvirt_xml = libxml2.parseFile('/var/lib/pacemaker-cloud/assemblies/%s.xml' % name)
                    libvirt_doc = libvirt_xml.serialize(None, 1);
                    libvirt_dom = self.libvirt_conn.createXML(libvirt_doc, 0)

                    self.session.methodSuccess(handle)

                elif methodName == "stop":
                    try:
                        ass = self.libvirt_conn.lookupByName(name)
                        ass.destroy()
                    except:
                        print '*** couldn\'t stop %s (already stopped?)' % name
                    self.session.methodSuccess(handle)

                elif methodName == "status":
                    status = 'Unknown'
                    try:
                        ass = self.libvirt_conn.lookupByName(name)
                        if ass.isActive():
                            status = 'Running'
                        else:
                            status = 'Stopped'
                    except:
                        status = 'Undefined'

                    handle.addReturnArgument("status", status)
                    self.session.methodSuccess(handle)

            except BaseException, e:
                self.session.raiseException(handle, "%r" % e)


    def setupSchema(self):
        """
        Create and register the schema for this agent.
        """
        package = "org.pacemakercloud"

        ##
        ## Declare a control object to test methods against.
        ##
        self.sch_control = Schema(SCHEMA_TYPE_DATA, package, "Vmlauncher")
        self.sch_control.addProperty(SchemaProperty("state", SCHEMA_DATA_STRING))
        self.sch_control.addProperty(SchemaProperty("methodCount", SCHEMA_DATA_INT))

        startMethod = SchemaMethod("start", desc="Start Assmebly")
        startMethod.addArgument(SchemaProperty("name", SCHEMA_DATA_STRING, direction=DIR_IN))
        startMethod.addArgument(SchemaProperty("uuid", SCHEMA_DATA_STRING, direction=DIR_IN))
        self.sch_control.addMethod(startMethod)

        stopMethod = SchemaMethod("stop", desc="Stop Assmebly")
        stopMethod.addArgument(SchemaProperty("name", SCHEMA_DATA_STRING, direction=DIR_IN))
        stopMethod.addArgument(SchemaProperty("uuid", SCHEMA_DATA_STRING, direction=DIR_IN))
        self.sch_control.addMethod(stopMethod)

        statusMethod = SchemaMethod("status", desc="Assmebly Status")
        statusMethod.addArgument(SchemaProperty("name", SCHEMA_DATA_STRING, direction=DIR_IN))
        statusMethod.addArgument(SchemaProperty("uuid", SCHEMA_DATA_STRING, direction=DIR_IN))
        statusMethod.addArgument(SchemaProperty("status", SCHEMA_DATA_STRING, direction=DIR_OUT))
        self.sch_control.addMethod(statusMethod)

        ##
        ## Register our schemata with the agent session.
        ##
        self.session.registerSchema(self.sch_control)

    def populateData(self):
        """
        Create a control object and give it to the agent session to manage.
        """
        self.control = Data(self.sch_control)
        self.control.state = "OPERATIONAL"
        self.control.methodCount = 0
        self.controlAddr = self.session.addData(self.control, "singleton")


try:
    agent = VMLauncher("localhost:49000")
    agent.setupSchema()
    agent.populateData()
    agent.run()  # Use agent.start() to launch the agent in a separate thread
    agent.shutdown()
except Exception, e:
    print "Exception Caught:", e


