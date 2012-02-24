#!/usr/bin/env python
#
# Copyright (C) 2012 Red Hat, Inc.
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
import functools
import errno
import socket
from tornado.netutil import bind_unix_socket
from tornado.ioloop import IOLoop
#import httpserver

def main():

    def handle_message(sock, fd, events):
        if IOLoop.ERROR & events:
            IOLoop.instance().remove_handler(fd)
            sock.close()
            print('closing connection')
            return

        print('received_message')
        chunk = sock.recv(1024)
        print(chunk)

    def handle_connection(sock, fd, events):
        print('handling new connection')
        try:
            (connection, address) = sock.accept()
        except socket.error, e:
            if e.args[0] in (errno.EWOULDBLOCK, errno.EAGAIN):
                return
            raise

        message_callback = functools.partial(handle_message, connection)
        IOLoop.instance().add_handler(connection.fileno(),
                                  message_callback,
                                  IOLoop.ERROR|IOLoop.READ)

    unix_socket = bind_unix_socket('pacemaker-cloud-cped')
    accept_callback = functools.partial(handle_connection, unix_socket)
    IOLoop.instance().add_handler(unix_socket.fileno(),
                                  accept_callback,
                                  IOLoop.READ)

    IOLoop.instance().start()

if __name__ == '__main__':
    main()

