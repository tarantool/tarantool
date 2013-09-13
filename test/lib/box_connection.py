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
import errno
import ctypes
import socket
import struct
import warnings

from tarantool_connection import TarantoolConnection

try:
    tnt_py = os.path.dirname(os.path.abspath(__file__))
    tnt_py = os.path.join(tnt_py, 'tarantool-python/src')
    sys.path.append(tnt_py)
    from tarantool import Connection as tnt_connection
    from tarantool import Schema
except ImportError:
    sys.stderr.write("\n\nNo tarantool-python library found\n")
    sys.exit(1)

class BoxConnection(TarantoolConnection):
    def __init__(self, host, port):
        super(BoxConnection, self).__init__(host, port)
        self.py_con = tnt_connection(host, port, connect_now=False)
        self.py_con.error = False
        self.sort = False
 
    def connect(self):
        self.py_con.connect()
    
    def disconnect(self):
        self.py_con.close()
    
    def reconnect(self):
        self.disconnect()
        self.connect() 

    def set_schema(self, schemadict):
        self.py_con.schema = Schema(schemadict)

    def check_connection(self):
        rc = self.py_con._recv(self.py_con._socket.fileno(), '', 0, socket.MSG_DONTWAIT)
        if ctypes.get_errno() == errno.EAGAIN:
            ctypes.set_errno(0)
            return True
        return False

    def execute(self, command, silent=True):
        return self.execute_no_reconnect(command, silent)

    def execute_no_reconnect(self, command, silent=True):
        statement = sql.parse("sql", command)
        if statement == None:
            return "You have an error in your SQL syntax\n"
        statement.sort = self.sort
        
        response = None
        request = statement.pack(self.py_con)
        with warnings.catch_warnings():
            warnings.simplefilter("ignore")
            response = self.py_con._send_request(request, False)

        if not silent:
            print command
            print statement.unpack(response)

        return statement.unpack(response)
