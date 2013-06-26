#!/usr/bin/env python
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

import os
import sys
import time
import string
import shutil
import os.path
import argparse

from lib.test_suite import TestSuite

#
# Run a collection of tests.
#

class Options:
    """Handle options of test-runner"""
    def __init__(self):
        """Add all program options, with their defaults."""

        parser = argparse.ArgumentParser(
                description = "Tarantool regression test suite front-end.")

        parser.epilog = "For a complete description, use 'pydoc ./" +\
                os.path.basename(sys.argv[0]) + "'"

        parser.add_argument(
                "tests",
                metavar="test",
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
                default = [],
                help = """List of tests suites to look for tests in. Default: "" -
                means find all available.""")

        parser.add_argument(
                "--force",
                dest = "is_force",
                action = "store_true",
                default = False,
                help = """Go on with other tests in case of an individual test failure.
                Default: false.""")

        parser.add_argument(
                "--start-and-exit",
                dest = "start_and_exit",
                action = "store_true",
                default = False,
                help = """Start the server from the first specified suite and
                exit without running any tests. Default: false.""")

        parser.add_argument(
                "--gdb",
                dest = "gdb",
                action = "store_true",
                default = False,
                help = """Start the server under 'gdb' debugger.
                See also --start-and-exit. This option is mutually exclusive with
                --valgrind. Default: false.""")

        parser.add_argument(
                "--valgrind",
                dest = "valgrind",
                action = "store_true",
                default = False,
                help = "Run the server under 'valgrind'. Default: false.")

        parser.add_argument(
                "--builddir",
                dest = "builddir",
                default = "..",
                help = """Path to project build directory. Default: " + "../.""")

        parser.add_argument(
                "--vardir",
                dest = "vardir",
                default = "var",
                help = """Path to data directory. Default: var.""")

        parser.add_argument(
                "--mem",
                dest = "mem",
                action = "store_true",
                default = False,
                help = """Run test suite in memory, using tmpfs or ramdisk.
                Is used only if vardir is not an absolute path. In that case
                vardir is sym-linked to /dev/shm/<vardir>.
                Linux only. Default: false.""")

        self.args = parser.parse_args()
        self.check()

    def check(self):
        """Check the arguments for correctness."""
        check_error = False
        if self.args.gdb and self.args.valgrind:
            print "Error: option --gdb is not compatible with option --valgrind"
            check_error = True
        if check_error:
            exit(-1)


def setenv():
    os.putenv("TARANTOOL_PLUGIN_DIR",
        string.join(
            (
                os.path.join(os.getcwd(), '../src/plugins/mysql'),
                os.path.join(os.getcwd(), '../src/plugins/pg')
            ),
            ':'
        )
    )

#######################################################################
# Program body
#######################################################################

def main():
    setenv()
    options = Options()
    oldcwd = os.getcwd()
    # Change the current working directory to where all test
    # collections are supposed to reside
    # If script executed with (python test-run.py) dirname is ''
    # so we need to make it .
    path = os.path.dirname(sys.argv[0])
    if not path:
        path = '.'
    os.chdir(path)

    failed_tests = 0

    try:
        print "Started", " ".join(sys.argv)
        suite_names = []
        if options.args.suites != []:
            suite_names = options.args.suites
        else:
            for root, dirs, names in os.walk(os.getcwd()):
                if "suite.ini" in names:
                    suite_names.append(os.path.basename(root))

        suites = [TestSuite(suite_name, options.args) for suite_name in sorted(suite_names)]
        
        for suite in suites:
            failed_tests += suite.run_all()
    except RuntimeError as e:
        print "\nFatal error: {0}. Execution aborted.".format(e)
        if options.args.gdb:
            time.sleep(100)
        return (-1)
    finally:
        os.chdir(oldcwd)

    return -failed_tests

if __name__ == "__main__":
  exit(main())
