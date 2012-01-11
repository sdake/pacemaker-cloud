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
import cqpid
import qmf2
import threading
from pcloudsh import pcmkconfig

class EventReceiver(qmf2.ConsoleHandler):

    def __init__(self, session):
        qmf2.ConsoleHandler.__init__(self, session)
        self.conf = pcmkconfig.Config()

    def eventRaised(self, agent, data, timestamp, severity):
        props = data.getProperties()
        reason = props['reason']
        state = props['state']

        try:
            service = props['service']
        except:
            service = ""

        try:
            assembly = props['assembly']
        except:
            assembly = ""

        try:
          deployable = props['deployable']
        except:
            deployable = ""

        print 'reasaon %s' % reason
        if reason == 'all assemblies active':
            script = '%s/%s.sh' % (self.conf.dbdir, deployable)
            if os.access(script, os.R_OK):
                os.system('%s %s' % (script, state))

        if reason == 'All good':
            print 'The assembly %s in deployable %s is ACTIVE.' % (assembly, deployable)

        if reason == 'Started OK':
            print 'The resource %s in assembly %s in deployable %s is ACTIVE.' % (service, assembly, deployable)

        if reason == 'monitor failed':
            print 'The resource %s in assembly %s in deployable %s FAILED.' % (service, assembly, deployable)

        if reason == 'all assemblies active':
            print 'The deployable %s is ACTIVE.' % deployable

        if reason == 'Not reachable':
            print 'The assembly %s in deployable %s FAILED.' % (assembly, deployable)

        if reason == 'change in assembly state':
            print 'The deployable %s is RECOVERING.' % deployable

        if reason == 'escalating service failure':
            print 'A service recovery escalation terminated assembly %s in deployable %s.' % (assembly, deployable)

        if reason == 'assembly failure escalated to deployable':
            print 'An assembly recovery escalation terminated deployable %s.' % deployable
