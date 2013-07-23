__author__ = "Konstantin Osipov <kostja.osipov@gmail.com>"

# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

import socket
import sys
import cStringIO
import errno

class TarantoolConnection(object):
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.is_connected = False
        self.stream = cStringIO.StringIO()
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.setsockopt(socket.SOL_TCP, socket.TCP_NODELAY, 1)

    def connect(self):
        self.socket.connect((self.host, self.port))
        self.is_connected = True

    def disconnect(self):
        if self.is_connected:
            self.socket.close()
            self.is_connected = False

    def reconnect(self):
        self.disconnect()
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.setsockopt(socket.SOL_TCP, socket.TCP_NODELAY, 1)
        self.connect()

    def opt_reconnect(self):
        """ On a socket which was disconnected, recv of 0 bytes immediately
            returns with no data. On a socket which is alive, it returns EAGAIN.
            Make use of this property and detect whether or not the socket is
            dead. Reconnect a dead socket, do nothing if the socket is good."""
        try:
            if self.socket.recv(0, socket.MSG_DONTWAIT) == '':
                self.reconnect()
        except socket.error as e:
            if e.errno == errno.EAGAIN:
                pass
            else:
                self.reconnect()

    def execute(self, command, silent=True):
        self.opt_reconnect()
        return self.execute_no_reconnect(command, silent)

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, type, value, tb):
        self.disconnect()
 
    def __call__(self, command, silent=False, simple=False):
        return self.execute(command, silent)
