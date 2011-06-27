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
import socket
import fcntl
import struct

class Ifconfig(object):
    def __init__(self, ifname):

        self.ifreq = {}
        self.ifreq['ifname'] = ifname
        sock  = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.ifreq['addr']    = self._ifinfo(sock, 0x8915, ifname) # SIOCGIFADDR
            self.ifreq['brdaddr'] = self._ifinfo(sock, 0x8919, ifname) # SIOCGIFBRDADDR
            self.ifreq['netmask'] = self._ifinfo(sock, 0x891b, ifname) # SIOCGIFNETMASK
            self.ifreq['hwaddr']  = self._ifinfo(sock, 0x8927, ifname) # SIOCSIFHWADDR
        except:
            pass
        sock.close()

    def _ifinfo(self, sock, addr, ifname):
        iface = struct.pack('256s', ifname[:15])
        info  = fcntl.ioctl(sock.fileno(), addr, iface)
        if addr == 0x8927:
            hwaddr = []
            for char in info[18:24]:
                hwaddr.append(hex(ord(char))[2:])
            return ':'.join(hwaddr)
        else:
            return socket.inet_ntoa(info[20:24])

    def __str__(self):
        return str(self.ifreq)

    def addr_get(self):
        return self.ifreq['addr']

    def broadcast_get(self):
        return self.ifreq['brdaddr']

    def netmask_get(self):
        return self.ifreq['netmask']

    def hwaddr_get(self):
        return self.ifreq['hwaddr']

if __name__ == '__main__':
    info = Ifconfig('virbr0')
    print info

    print 'IP addr is %s' % info.addr_get()

