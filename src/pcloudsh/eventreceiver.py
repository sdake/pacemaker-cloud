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
import sys
import qmf2
from pcloudsh import pcmkconfig

class EventReceiver(qmf2.ConsoleHandler):

    def __init__(self, session):
        qmf2.ConsoleHandler.__init__(self, session)
        self.conf = pcmkconfig.Config()

    def eventRaised(self, agent, data, timestamp, severity):
        props = data.getProperties()
        reason = props['reason']
        state = props['state']

        service = props.get('service','')
        assembly = props.get('assembly','')
        deployable = props.get('deployable','')

        if sys.stdout.isatty():
            ACTIVE = "\x1b[1;37;42mACTIVE\x1b[0m"
            RECOVERING ="\x1b[30;43mRECOVERING\x1b[0m"
            FAILED = "\x1b[01;37;41mFAILED\x1b[0m"
        else:
            ACTIVE = "ACTIVE"
            RECOVERING ="RECOVERING"
            FAILED = "FAILED"

        event_desc = {
            'All good':
              'The assembly [%(assembly)s] in deployable [%(deployable)s] is %(ACTIVE)s.',
            'Started OK':
              'The resource [%(service)s] in assembly [%(assembly)s] in deployable [%(deployable)s] is %(ACTIVE)s.',
            'monitor failed':
              'The resource [%(service)s] in assembly [%(assembly)s] in deployable [%(deployable)s] %(FAILED)s.',
            'all assemblies active':
              'The deployable [%(deployable)s] is %(ACTIVE)s.',
            'Not reachable':
              'The assembly [%(assembly)s] in deployable [%(deployable)s] %(FAILED)s.',
            'change in assembly state':
              'The deployable [%(deployable)s] is %(RECOVERING)s.',
            'escalating service failure':
              'A service recovery escalation terminated assembly [%(assembly)s] in deployable [%(deployable)s].',
            'assembly failure escalated to deployable':
              'An assembly recovery escalation terminated deployable [%(deployable)s].'
        }

        print event_desc.get(reason,'') % locals()

        # Run helper script in case IPs have changed
        if reason == 'all assemblies active':
            script = '%s/%s.sh' % (self.conf.dbdir, deployable)
            if os.access(script, os.R_OK):
                os.system('%s %s' % (script, state))
