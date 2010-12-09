#! /usr/bin/python 
"""A simplistic client for tarantool administrative console.

On startup, establishes a connection to tarantool server.
Then, reads commands from stdin, and sends them to stdout.
The commands are echoed to stdout. The results are echoed
to stdout as well, prefixed with "r> ".
"""
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

import argparse
import socket
import sys
import string

class Options:
  def __init__(self):
    """Add all program options, with their defaults."""

    parser = argparse.ArgumentParser(
        description = "Tarantool regression test suite client.")

    parser.add_argument(
        "--host",
        dest = 'host',
        metavar = "host",
        default = "localhost",
        help = "Host to connect to. Default: localhost")

    parser.add_argument(
        "--port",
        dest = "port",
        default = 33015,
        help = "Server port to connect to. Default: 33015")

    parser.add_argument(
        "--result-prefix",
        metavar = "prefix",
        dest = "result_prefix",
        help = """Skip input lines that have the given prefix (e.g. "r> ".
        Prepend the prefix to all output lines. If not set, nothing is
        skipped and output is printed as-is. This option is used
        to pipe in .test files, automatically skipping test output.
        Without this option the program may be used as an interactive
        client. See also --prompt.""")

    parser.add_argument(
        "--prompt",
        metavar = "prompt",
        dest = "prompt",
        default = "\033[92mtarantool> \033[0m",
        help = """Command prompt. Set to "" for no prompt. Default:
        tarantool> """)
    
    self.args = parser.parse_args()


class Connection:
  def __init__(self, host, port):
    self.host = host
    self.port = port
    self.is_connected = False

  def connect(self):
    self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    self.socket.connect((self.host, self.port))
    self.is_connected = True

  def disconnect(self):
    if self.is_connected:
      self.socket.close()
      self.is_connected = False

  def execute(self, command):
    self.socket.sendall(command)

    bufsiz = 4096
    res = ""

    while True:
      buf = self.socket.recv(bufsiz)
      if not buf:
	break
      res+= buf;
      if res.rfind("---\n"):
	break

    return res

  def __enter__(self):
    self.connect()
    return self

  def __exit__(self, type, value, tb):
    self.disconnect()


def main():
  options = Options()
  try:
    with Connection(options.args.host, options.args.port) as con:
      result_prefix = options.args.result_prefix
      prompt = options.args.prompt
      if prompt != "":
        sys.stdout.write(prompt)
      for line in iter(sys.stdin.readline, ""):
        if result_prefix != None and line.find(result_prefix) == 0:
          continue
        output = con.execute(line)
        if result_prefix != None:
          print line, result_prefix, string.join(output.split("\n"),
                                               "\n" + result_prefix)
        else:
          sys.stdout.write(output)
        sys.stdout.write(prompt)

    return 0
  except (RuntimeError, socket.error, KeyboardInterrupt) as e:
    print "Fatal error: ", repr(e)
    return -1

if __name__ == "__main__":
  exit(main())

