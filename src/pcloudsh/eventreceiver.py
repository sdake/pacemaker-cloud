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
import qmf2
import threading

class EventReceiver(qmf2.ConsoleHandler):

    def __init__(self, session):
        qmf2.ConsoleHandler.__init__(self, session)

    def eventRaised(self, agent, data, timestamp, severity):
        print "Event: %r" % (data.getProperties())

class EventRunner(threading.Thread):

    def __init__(self):
        threading.Thread.__init__(self)

        self.connection = cqpid.Connection('localhost:49000', '')
        self.connection.open()

        self.session = qmf2.ConsoleSession(self.connection)
        self.session.open()
        self.session.setAgentFilter("[eq, _vendor, [quote, 'pacemakercloud.org']]")

        self.receiver = EventReceiver(self.session)

    def run(self):
        self.receiver.run()

