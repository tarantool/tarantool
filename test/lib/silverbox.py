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
import sql
import struct
from tarantool_connection import TarantoolConnection

class Silverbox(TarantoolConnection):
  def recvall(self, length):
    res = ""
    while len(res) < length:
      buf = self.socket.recv(length - len(res))
      res = res + buf
    return res

  def execute_no_reconnect(self, command, noprint=True):
    statement = sql.parse("sql", command)
    if statement == None:
      return "You have an error in your SQL syntax\n"

    payload = statement.pack()
    header = struct.pack("<lll", statement.reqeust_type, len(payload), 0)

    self.socket.sendall(header)
    if len(payload):
      self.socket.sendall(payload)

    IPROTO_HEADER_SIZE = 12

    header = self.recvall(IPROTO_HEADER_SIZE)

    response_len = struct.unpack("<lll", header)[1]

    if response_len:
      response = self.recvall(response_len)
    else:
      response = None

    return statement.unpack(response) + "\n"

