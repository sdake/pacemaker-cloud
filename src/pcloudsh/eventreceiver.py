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
        print "Event: %r" % (props)
        if not 'assembly' in props:
            dep = props['deployable']
            state = props['state']
            script = '%s/%s.sh' % (self.conf.dbdir, dep)
            if os.access(script, os.R_OK):
                os.system('%s %s' % (script, state))
