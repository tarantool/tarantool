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
import os
import sql
import sys
import socket
import struct
from tarantool_connection import TarantoolConnection

try:
    tnt_py = os.path.dirname(os.path.abspath(__file__))
    tnt_py = os.path.join(tnt_py, 'tarantool-python/src')
    sys.path.append(tnt_py)
    from tarantool import Connection as tnt_connection
except ImportError:
    raise
    sys.stderr.write("\n\nNo tarantool-python library found\n")
    sys.exit(1)

class BoxConnection(TarantoolConnection):
    def __init__(self, host, port):
        super(BoxConnection, self).__init__(host, port)
        self.py_con = tnt_connection(host, port, connect_now=False)
        self.py_con.error = False
        self.sort = False

    def recvall(self, length):
        res = ""
        while len(res) < length:
            buf = self.socket.recv(length - len(res))
            if not buf:
                raise RuntimeError("Got EOF from socket, the server has "
                                   "probably crashed")
            res = res + buf
        return res

    def execute_no_reconnect(self, command, silent=True):
        statement = sql.parse("sql", command)
        if statement == None:
            return "You have an error in your SQL syntax\n"
        statement.sort = self.sort

        request = statement.pack(self.py_con)
        response = self.py_con._send_request(request, False)

        if not silent:
            print command
            print statement.unpack(response)

        return statement.unpack(response)
