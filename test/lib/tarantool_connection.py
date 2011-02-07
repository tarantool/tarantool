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
import string
import cStringIO
import yaml
import re
import sql
import struct

is_admin_re = re.compile("^\s*(show|save|exec|exit|reload|help)", re.I)

class AdminConnection:
  def __init__(self, host, port):
    self.host = host
    self.port = port
    self.is_connected = False
    self.stream = cStringIO.StringIO()

  def connect(self):
    self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    self.socket.setsockopt(socket.SOL_TCP, socket.TCP_NODELAY, 1)
    self.socket.connect((self.host, self.port))
    self.is_connected = True

  def disconnect(self):
    if self.is_connected:
      self.socket.close()
      self.is_connected = False

  def reconnect(self):
    self.disconnect()
    self.connect()

  def opt_reconnect(self):
    try:
      if self.socket.recv(0, socket.MSG_DONTWAIT) == '':
        self.reconnect()
    except socket.error:
      pass

  def execute(self, command):
    self.opt_reconnect()
    return self.execute_no_reconnect(command)

  def execute_no_reconnect(self, command):
    self.socket.sendall(command)

    bufsiz = 4096
    res = ""

    while True:
      buf = self.socket.recv(bufsiz)
      if not buf:
        break
      res = res + buf;
      if (res.rfind("\r\n...\r\n") >= 0):
        break

    # validate yaml by parsing it
    yaml.load(res)

    return res

  def write(self, fragment):
    """This is to support print >> admin, "command" syntax.
    For every print statement, write is invoked twice: one to
    write the command itself, and another to write \n. We should
    accumulate all writes until we receive \n. When we receive it,
    we execute the command, and rewind the stream."""
       
    newline_pos = fragment.rfind("\n")
    while newline_pos >= 0:
      self.stream.write(fragment[:newline_pos+1])
      statement = self.stream.getvalue()
      sys.stdout.write(statement)
      sys.stdout.write(self.execute(statement))
      fragment = fragment[newline_pos+1:]
      newline_pos = fragment.rfind("\n")
      self.stream.seek(0)
      self.stream.truncate()

    self.stream.write(fragment)

  def __enter__(self):
    self.connect()
    return self

  def __exit__(self, type, value, tb):
    self.disconnect()

class DataConnection(AdminConnection):

  def recvall(self, length):
    res = ""
    while len(res) < length:
      buf = self.socket.recv(length - len(res))
      res = res + buf
    return res

  def execute_no_reconnect(self, command):
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

