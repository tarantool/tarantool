import os
import os.path
import sys
import stat
import glob
import ConfigParser
import subprocess
import collections
import difflib
import filecmp
import shlex
from tarantool_silverbox_server import TarantoolSilverboxServer 

class TestRunException(RuntimeError):
  """A common exception to use across the program."""
  def __init__(self, message):
    self.message = message
  def __str__(self):
    return self.message

class Test:
  """An individual test file. A test can run itself, and remembers
  its completion state."""
  def __init__(self, name, suite_ini):
    """Initialize test properties: path to test file, path to 
    temporary result file, path to the client program, test status."""
    self.name = name
    self.result = name.replace(".test", ".result")
    self.suite_ini = suite_ini
    self.is_executed = False
    self.is_client_ok = None
    self.is_equal_result = None

  def passed(self):
    """Return true if this test was run successfully."""
    return self.is_executed and self.is_client_ok and self.is_equal_result

  def run(self):
    """Execute the client program, giving it test as stdin,
    result as stdout. If the client program aborts, print
    its output to stdout, and raise an exception. Else, comprare
    result and reject files. If there is a difference, print it to
    stdout and raise an exception. The exception is raised only
    if is_force flag is not set."""

    def subst_test_env(arg):
      if len(arg) and arg[0] == '$':
        return self.suite_ini[arg[1:]]
      else:
        return arg

    client = os.path.join(".", self.suite_ini["client"])
    args = map(subst_test_env, shlex.split(client))

    sys.stdout.write(self.name)
# for better diagnostics in case of a long-running test
    sys.stdout.flush()

    with open(self.name, "r") as test:
      with open(self.result, "w+") as result:
        self.is_client_ok = \
          subprocess.call(args,
                          stdin = test, stdout = result) == 0
    
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
      if not self.suite_ini["is_force"]:
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
            
class TarantoolConfigFile:
  """ConfigParser can't read files without sections, work it around"""
  def __init__(self, fp, section_name):
    self.fp = fp
    self.section_name = "[" + section_name + "]"
  def readline(self):
    if self.section_name:
      section_name = self.section_name
      self.section_name = None
      return section_name
    # tarantool.cfg puts string values in quotes 
    return self.fp.readline().replace("\"", '')


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

# read the suite config
    config = ConfigParser.ConfigParser()
    config.read(os.path.join(self.path, "suite.ini"))
    self.ini = dict(config.items("default"))
# import the necessary module for test suite client

    self.ini["host"] = "localhost"
    self.ini["is_force"] = self.args.is_force

# now read the server config, we need some properties from it

    with open(os.path.join(self.path, self.ini["config"])) as fp:
      dummy_section_name = "tarantool_silverbox"
      config.readfp(TarantoolConfigFile(fp, dummy_section_name))
      self.ini["pidfile"] = config.get(dummy_section_name, "pid_file")
      self.ini["port"] = config.get(dummy_section_name, "admin_port")

    print "Collecting tests in \"" + self.path + "\": " +\
      self.ini["description"] + "."

    for test_name in glob.glob(os.path.join(self.path, "*.test")):
      for test_pattern in self.args.tests:
        if test_name.find(test_pattern) != -1:
          self.tests.append(Test(test_name, self.ini))
    print "Found " + str(len(self.tests)) + " tests."

  def run_all(self):
    """For each file in the test suite, run client program
    assuming each file represents an individual test."""
    server = TarantoolSilverboxServer(self.args,
                                      os.path.join(self.path,
                                                   self.ini["config"]),
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
    self.ini["server"] = server.abspath_to_exe

    for test in self.tests:
      test.run()
      if not test.passed():
        failed_tests.append(test.name)

    print shortsep
    if len(failed_tests):
      print "Failed {0} tests: {1}.".format(len(failed_tests),
                                            ", ".join(failed_tests))
    server.stop();

