#! /usr/bin/python
"""Tarantool regression test suite front-end."""

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
import os.path
import sys
from lib.test_suite import TestSuite, TestRunException

#
# Run a collection of tests.
#
# @todo
# --gdb
# put class definitions into separate files

class Options:
  """Handle options of test-runner"""
  def __init__(self):
    """Add all program options, with their defaults. We assume
    that the program is started from the directory where it is
    located"""

    parser = argparse.ArgumentParser(
        description = "Tarantool regression test suite front-end. \
        This program must be started from its working directory (" +
        os.path.abspath(os.path.dirname(sys.argv[0])) + ").")

    parser.epilog = "For a complete description, use 'pydoc ./" +\
        os.path.basename(sys.argv[0]) + "'"

    parser.add_argument(
        "tests",
        metavar="list of tests",
        nargs="*",
        default = [""],
        help="""Can be empty. List of test names, to look for in suites. Each
        name is used as a substring to look for in the path to test file,
        e.g. "show" will run all tests that have "show" in their name in all
        suites, "box/show" will only enable tests starting with "show" in
        "box" suite. Default: run all tests in all specified suites.""")

    parser.add_argument(
        "--suite",
        dest = 'suites',
        metavar = "suite",
        nargs="*",
        default = ["box"],
        help = """List of tests suites to look for tests in. Default: "box"
        and "cmd".""")

    parser.add_argument(
        "--force",
        dest = "is_force",
        action = "store_true",
        default = False,
        help = "Go on with other tests in case of an individual test failure."
               " Default: false.")

    parser.add_argument(
        "--start-and-exit",
        dest = "start_and_exit",
        action = "store_true",
        default = False,
        help = "Start the server from the first specified suite and"
        "exit without running any tests. Default: false.")

    parser.add_argument(
        "--gdb",
        dest = "gdb",
        action = "store_true",
        default = False,
        help = "Start the server under 'gdb' debugger. Default: false."
        " See also --start-and-exit.")

    parser.add_argument(
        "--bindir",
        dest = "bindir",
        default = "../_debug_box",
        help = "Path to server binary."
               " Default: " + "../_debug_box.")

    parser.add_argument(
        "--vardir",
        dest = "vardir",
        default = "var",
        help = "Path to data directory. Default: var.")

    parser.add_argument(
        "--mem",
        dest = "mem",
        action = "store_true",
        default = False,
        help = """Run test suite in memory, using tmpfs or ramdisk.
        Is used only if vardir is not an absolute path. In that case
        vardir is sym-linked to /dev/shm/<vardir>.
        Linux only. Default: true""")

    self.check(parser)
    self.args = parser.parse_args()

  def check(self, parser):
    """Check that the program is started from the directory where
    it is located. This is necessary to minimize potential confusion
    with absolute paths, since all default paths are relative to the
    starting directory."""

    if not os.path.exists(os.path.basename(sys.argv[0])):
# print first 6 lines of help
      short_help = "\n".join(parser.format_help().split("\n")[0:6])
      print short_help
      exit(-1)


#######################################################################
# Program body
#######################################################################

def main():
  options = Options()

  try:
    print "Started", " ".join(sys.argv)
    suites = []
    for suite_name in options.args.suites:
      suites.append(TestSuite(suite_name, options.args))

    for suite in suites:
      suite.run_all()
  except RuntimeError as e:
    print "\nFatal error: {0}. Execution aborted.".format(e)
    return (-1)

  return 0

if __name__ == "__main__":
  exit(main())
