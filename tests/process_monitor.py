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
import subprocess
import time
import signal

class ProcessMonitor(object):

    def __init__(self, command):
        self.p = subprocess.Popen(command)
        self.executible = command[0]
        self.rc = None

    def __del__(self):
        self.stop()

    def is_running(self):
        if self.rc != None:
            return False
        self.rc = self.p.poll()
        if self.rc != None:
            return False
        else:
            return True

    def stop(self):
        if self.is_running():
            os.kill(self.p.pid, signal.SIGTERM)
            self.rc = self.p.wait()

    def retcode_get(self):
        return self.rc

    def __str__(self):
        if self.is_running():
            return '%s(%d) is running' % (self.executible, self.p.pid)
        else:
            return '%s(%d) exited with %d' % (self.executible, self.p.pid, self.rc)

