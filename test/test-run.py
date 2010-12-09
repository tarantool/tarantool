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
import os
import os.path
import signal
import sys
import stat
import glob
import shutil
import ConfigParser
import subprocess
import pexpect
import time
import collections
import difflib
import filecmp

#
# Run a collection of tests.
#
# @todo
# --gdb
# put class definitions into separate files

############################################################################
# Class definition 
############################################################################

class TestRunException(RuntimeError):
  """A common exception to use across the program."""
  def __init__(self, message):
    self.message = message
  def __str__(self):
    return self.message

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
        help = """List of tests suites to look for tests in. Default: "box".""")

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

    self.check(parser)
    self.args = parser.parse_args()

  def check(self, parser):
    """Check that the program is started from the directory where
    it is located. This is necessary to minimize potential confusion
    with absolute paths, since all default paths are relative to the
    starting directory."""

    if not os.path.exists(os.path.basename(sys.argv[0])):
      parser.print_help()
      exit(-1)


class Server:
  """Server represents a single server instance. Normally, the
  program operates with only one server, but in future we may add
  replication slaves. The server is started once at the beginning
  of each suite, and stopped at the end."""

  def __init__(self, args, config, pidfile):
    """Set server options: path to configuration file, pid file, exe, etc."""
    self.args = args
    self.path_to_config = config
    self.path_to_pidfile = os.path.join(args.vardir, pidfile)
    self.path_to_exe = None
    self.abspath_to_exe = None
    self.is_started = False

  def start(self):
    """Start server instance: check if the old one exists, kill it
    if necessary, create necessary directories and files, start
    the server. The server working directory is taken from 'vardir',
    specified in the prgoram options.
    Currently this is implemented for tarantool_silverbox only."""

    if not self.is_started:
      print "Starting the server..."

      if self.path_to_exe == None:
        self.path_to_exe = self.find_exe() 
        self.abspath_to_exe = os.path.abspath(self.path_to_exe)

      print "  Found executable at " + self.path_to_exe + "."

      print "  Creating and populating working directory in " +\
      self.args.vardir + "..." 

      if os.access(self.args.vardir, os.F_OK):
        print "  Found old vardir, deleting..."
        self.kill_old_server()
        shutil.rmtree(self.args.vardir, ignore_errors = True)

      os.mkdir(self.args.vardir)
      shutil.copy(self.path_to_config, self.args.vardir)

      subprocess.check_call([self.abspath_to_exe, "--init_storage"],
                            cwd = self.args.vardir,
                            stdout = subprocess.PIPE,
                            stderr = subprocess.PIPE)

      if self.args.start_and_exit:
        subprocess.check_call([self.abspath_to_exe, "--daemonize"],
                              cwd = self.args.vardir,
                              stdout = subprocess.PIPE,
                              stderr = subprocess.PIPE)
      else:
        self.server = pexpect.spawn(self.abspath_to_exe,
                                    cwd = self.args.vardir)
        self.logfile_read = sys.stdout
        self.server.expect_exact("entering event loop")

      version = subprocess.Popen([self.abspath_to_exe, "--version"],
                                 cwd = self.args.vardir,
                                 stdout = subprocess.PIPE).stdout.read().rstrip()

      print "Started {0} {1}.".format(os.path.basename(self.abspath_to_exe),
                                      version)

# Set is_started flag, to nicely support cleanup during an exception.
      self.is_started = True
    else:
      print "The server is already started."

  def stop(self):
    """Stop server instance. Do nothing if the server is not started,
    to properly shut down the server in case of an exception during
    start up."""
    if self.is_started:
      print "Stopping the server..."
      self.server.terminate()
      self.server.expect(pexpect.EOF)
      self.is_started = False
    else:
      print "The server is not started."

  def find_exe(self):
    """Locate server executable in the bindir. We just take
    the first thing looking like an exe in there."""

    if (os.access(self.args.bindir, os.F_OK) == False or
        stat.S_ISDIR(os.stat(self.args.bindir).st_mode) == False):
      raise TestRunException("Directory " + self.args.bindir +
                             " doesn't exist")

    for f in os.listdir(self.args.bindir):
      f = os.path.join(self.args.bindir, f)
      st_mode = os.stat(f).st_mode
      if stat.S_ISREG(st_mode) and st_mode & stat.S_IXUSR:
        return f
    raise TestRunException("Can't find server executable in " + 
                           self.args.bindir)

  def kill_old_server(self):
    """Kill old server instance if it exists."""
    if os.access(self.path_to_pidfile, os.F_OK) == False:
      return # Nothing to do
    pid = 0
    with open(self.path_to_pidfile) as f:
      pid = int(f.read()) 
    print "  Found old server, pid {0}, killing...".format(pid)
    try:
      os.kill(pid, signal.SIGTERM)
      while os.kill(pid, 0) != -1:
        time.sleep(0.01)
    except OSError:
      pass

class Test:
  """An individual test file. A test can run itself, and remembers
  its completion state."""
  def __init__(self, name, client):
    """Initialize test properties: path to test file, path to 
    temporary result file, path to the client program, test status."""
    self.name = name
    self.client = os.path.join(".", client)
    self.result = name.replace(".test", ".result")
    self.is_executed = False
    self.is_client_ok = None
    self.is_equal_result = None

  def passed(self):
    """Return true if this test was run successfully."""
    return self.is_executed and self.is_client_ok and self.is_equal_result

  def run(self, is_force):
    """Execute the client program, giving it test as stdin,
    result as stdout. If the client program aborts, print
    its output to stdout, and raise an exception. Else, comprare
    result and reject files. If there is a difference, print it to
    stdout and raise an exception. The exception is raised only
    if is_force flag is not set."""

    sys.stdout.write("{0}".format(self.name))

    with open(self.name, "r") as test:
      with open(self.result, "w+") as result:
        self.is_client_ok = \
          subprocess.call([self.client], stdin = test, stdout = result) == 0
    
    self.is_executed = True

    if self.is_client_ok:
      self.is_equal_result = filecmp.cmp(self.name, self.result)

    if self.is_client_ok and self.is_equal_result:
      print "\t\t\t[ pass ]"
      os.remove(self.result)
    else:
      print "\t\t\t[ fail ]"
      where = ""
      if not self.is_client_ok:
        self.print_diagnostics()
        where = ": client execution aborted"
      else:
        self.print_unidiff()
        where = ": wrong test output"
      if not is_force:
        raise TestRunException("Failed to run test " + self.name + where)


  def print_diagnostics(self):
    """Print 10 lines of client program output leading to test
    failure. Used to diagnose a failure of the client program"""

    print "Test failed! Last 10 lines of the result file:"
    with open(self.result, "r+") as result:
      tail_10 = collections.deque(result, 10)
      for line in tail_10:
        sys.stdout.write(line)

  def print_unidiff(self):
    """Print a unified diff between .test and .result files. Used
    to establish the cause of a failure when .test differs
    from .result."""

    print "Test failed! Result content mismatch:"
    with open(self.name, "r") as test:
      with open(self.result, "r") as result:
        test_time = time.ctime(os.stat(self.name).st_mtime)
        result_time = time.ctime(os.stat(self.result).st_mtime)
        diff = difflib.unified_diff(test.readlines(),
                                    result.readlines(),
                                    self.name,
                                    self.result,
                                    test_time,
                                    result_time)
        for line in diff:
          sys.stdout.write(line)
            

class TestSuite:
  """Each test suite contains a number of related tests files,
  located in the same directory on disk. Each test file has
  extention .test and contains a listing of server commands,
  followed by their output. The commands are executed, and
  obtained results are compared with pre-recorded output. In case
  of a comparision difference, an exception is raised. A test suite
  must also contain suite.ini, which describes how to start the
  server for this suite, the client program to execute individual
  tests and other suite properties. The server is started once per
  suite."""

  def __init__(self, suite_path, args):
    """Initialize a test suite: check that it exists and contains
    a syntactically correct configuration file. Then create
    a test instance for each found test."""
    self.path = suite_path
    self.args = args
    self.tests = []

    if os.access(self.path, os.F_OK) == False:
      raise TestRunException("Suite \"" + self.path + "\" doesn't exist")

    config = ConfigParser.ConfigParser()
    config.read(os.path.join(self.path, "suite.ini"))
    self.ini = dict(config.items("default"))
    print "Collecting tests in \"" + self.path + "\": " +\
    self.ini["description"] + "."

    for test_name in glob.glob(os.path.join(self.path, "*.test")):
      for test_pattern in self.args.tests:
        if test_name.find(test_pattern) != -1:
          self.tests.append(Test(test_name, self.ini["client"]))
    print "Found " + str(len(self.tests)) + " tests."

  def run_all(self):
    """For each file in the test suite, run client program
    assuming each file represents an individual test."""
    server = Server(self.args, os.path.join(self.path, self.ini["config"]),
      self.ini["pidfile"])
    server.start()
    if self.args.start_and_exit:
      print "  Start and exit requested, exiting..."
      exit(0)

    longsep = "=============================================================================="
    shortsep = "------------------------------------------------------------"
    print longsep
    print "TEST\t\t\t\tRESULT"
    print shortsep
    failed_tests = []

    for test in self.tests:
      test.run(self.args.is_force)
      if not test.passed():
        failed_tests.append(test.name)

    print shortsep
    if len(failed_tests):
      print "Failed {0} tests: {1}.".format(len(failed_tests),
                                            ", ".join(failed_tests))
    server.stop();

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
    print "\nFatal error: ", e, ". Execution aborted."
    return (-1)

  return 0

if __name__ == "__main__":
  exit(main())
