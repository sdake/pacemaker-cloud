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

class Deployable(object):

    def __init__(self, name):
        self.name = name
        self.assemblies = {}

    def assembly_add(self, ass):
        self.assemblies[ass.name] = ass

    def start(self):
        for n, a in self.assemblies.iteritems():
            a.start()

    def stop(self):
        for n, a in self.assemblies.iteritems():
            a.stop()
