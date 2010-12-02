#! /usr/bin/python 

__author__ = "Konstantin Osipov <kostja.osipov@gmail.com>"

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

    self.args = parser.parse_args()


class Connection:
  def __init__(self, host, port):
    self.host = host
    self.port = port
    self.is_connected = False

  def connect(self):
    self.socket = socket.socket(
      socket.AF_INET, socket.SOCK_STREAM)
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
      res_sep = "r> "
      for line in iter(sys.stdin.readline, ""):
	if line.find(res_sep) == 0:
	  continue
	print line,
	output = con.execute(line)
        print res_sep, string.join(output.split("\n"), "\n"+res_sep)
    return 0
  except (RuntimeError, socket.error) as e:
    print "Fatal error: ", repr(e)
    return -1

if __name__ == "__main__":
  exit(main())

