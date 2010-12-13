#! /usr/bin/python 
"""Test stdout/stdin interaction of a program".

Accepts a path to a UNIX command line program to test.
Reads command line options from stdin (one set of options per
one line of input), executes the program, and prints the output
to stdout, prefixed with r>. """

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
import shlex
import subprocess
import sys
import string

def main():
  parser = argparse.ArgumentParser(
      description = "Test command line options of a program.")

  parser.add_argument(
      "bin",
      help = """Path to the binary to test. Command line options are
      read from stdin, one line at a time, the program is run
      with the options, the output is sent to stdout.""")

  parser.add_argument(
      "--result-prefix",
      metavar = "prefix",
      dest = "result_prefix",
      help = """Skip input lines that have the given prefix (e.g. "r> ".
      Prepend the prefix to all output lines. If not set, nothing is
      skipped and output is printed as-is. This option is used
      to pipe in .test files, automatically skipping test output.
      Without this option the program may be used as an interactive
      client.""")

  args = parser.parse_args()

  try:
    result_prefix = args.result_prefix

    for line in iter(sys.stdin.readline, ""):
      if result_prefix != None and line.find(result_prefix) == 0:
        continue
      path = string.join([args.bin, line])
      output = subprocess.Popen(shlex.split(path),
                                stdout = subprocess.PIPE,
                                stderr = subprocess.PIPE).stdout.read()
      if result_prefix != None:
        print line, result_prefix, string.join(output.split("\n"),
                                               "\n" + result_prefix)
      else:
        sys.stdout.write(output)

    return 0
  except RuntimeError as e:
    print "Fatal error: ", repr(e)
    return -1

if __name__ == "__main__":
  exit(main())

