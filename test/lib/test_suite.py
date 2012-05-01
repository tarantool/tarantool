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
import time
from server import Server
import tarantool_preprocessor
import re
import cStringIO
import string
import traceback

class FilteredStream:

    """Helper class to filter .result file output"""
    def __init__(self, filename):
        self.stream = open(filename, "w+")
        self.filters = []

    def write(self, fragment):
        """Apply all filters, then write result to the undelrying stream.
        Do line-oriented filtering: the fragment doesn't have to represent
        just one line."""
        fragment_stream = cStringIO.StringIO(fragment)
        for line in fragment_stream:
            original_len = len(line.strip())
            for pattern, replacement in self.filters:
                line = re.sub(pattern, replacement, line)
                # don't write lines that are completely filtered out:
                if original_len and len(line.strip()) == 0:
                    return
            self.stream.write(line)

    def push_filter(self, pattern, replacement):
        self.filters.append([pattern, replacement])

    def pop_filter(self):
        self.filters.pop()

    def clear_all_filters(self):
        filters = []

    def close(self):
        self.clear_all_filters()
        self.stream.close()


def check_valgrind_log(path_to_log):
    """ Check that there were no warnings in the log."""
    return os.path.getsize(path_to_log) != 0


def print_tail_n(filename, num_lines):
    """Print N last lines of a file."""
    with open(filename, "r+") as logfile:
        tail_n = collections.deque(logfile, num_lines)
        for line in tail_n:
            sys.stdout.write(line)


class Test:
    """An individual test file. A test object can run itself
    and remembers completion state of the run."""

    def __init__(self, name, args, suite_ini):
        """Initialize test properties: path to test file, path to
        temporary result file, path to the client program, test status."""

        self.name = name
        self.args = args
        self.suite_ini = suite_ini
        self.result = name.replace(".test", ".result")
        self.tmp_result = os.path.join(self.args.vardir,
                                       os.path.basename(self.result))
	self.reject = "{0}/test/{1}".format(self.args.builddir,
			                    name.replace(".test", ".reject"))
        self.is_executed = False
        self.is_executed_ok = None
        self.is_equal_result = None
        self.is_valgrind_clean = True

    def passed(self):
        """Return true if this test was run successfully."""

        return self.is_executed and self.is_executed_ok and self.is_equal_result

    def run(self, server):
        """Execute the test assuming it's a python program.
        If the test aborts, print its output to stdout, and raise
        an exception. Else, comprare result and reject files.
        If there is a difference, print it to stdout and raise an
        exception. The exception is raised only if is_force flag is
        not set."""

        diagnostics = "unknown"
        save_stdout = sys.stdout
        builddir = self.args.builddir
        try:
            sys.stdout = FilteredStream(self.tmp_result)
            stdout_fileno = sys.stdout.stream.fileno()
            execfile(self.name, dict(locals(), **server.__dict__))
            self.is_executed_ok = True
        except Exception as e:
            traceback.print_exc(e)
            diagnostics = str(e)
        finally:
            if sys.stdout and sys.stdout != save_stdout:
                sys.stdout.close()
            sys.stdout = save_stdout;

        self.is_executed = True

        if self.is_executed_ok and os.path.isfile(self.result):
            self.is_equal_result = filecmp.cmp(self.result, self.tmp_result)

        if self.args.valgrind:
            self.is_valgrind_clean = \
            check_valgrind_log(server.valgrind_log) == False

        if self.is_executed_ok and self.is_equal_result and self.is_valgrind_clean:
            print "[ pass ]"
            os.remove(self.tmp_result)
        elif (self.is_executed_ok and not self.is_equal_result and not
              os.path.isfile(self.result)):
            os.rename(self.tmp_result, self.result)
            print "[ NEW ]"
        else:
            os.rename(self.tmp_result, self.reject)
            print "[ fail ]"

            where = ""
            if not self.is_executed_ok:
                self.print_diagnostics(self.reject, "Test failed! Last 10 lines of the result file:")
                where = ": test execution aborted, reason '{0}'".format(diagnostics)
            elif not self.is_equal_result:
                self.print_unidiff()
                where = ": wrong test output"
            elif not self.is_valgrind_clean:
                os.remove(self.reject)
                self.print_diagnostics(server.valgrind_log, "Test failed! Last 10 lines of valgrind.log:")
                where = ": there were warnings in valgrind.log"

            if not self.args.is_force:
                raise RuntimeError("Failed to run test " + self.name + where)

    def print_diagnostics(self, logfile, message):
        """Print 10 lines of client program output leading to test
        failure. Used to diagnose a failure of the client program"""

        print message
        print_tail_n(logfile, 10)

    def print_unidiff(self):
        """Print a unified diff between .test and .result files. Used
        to establish the cause of a failure when .test differs
        from .result."""

        print "Test failed! Result content mismatch:"
        with open(self.result, "r") as result:
            with open(self.reject, "r") as reject:
                result_time = time.ctime(os.stat(self.result).st_mtime)
                reject_time = time.ctime(os.stat(self.reject).st_mtime)
                diff = difflib.unified_diff(result.readlines(),
                                            reject.readlines(),
                                            self.result,
                                            self.reject,
                                            result_time,
                                            reject_time)
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
        self.args = args
        self.tests = []
        self.ini = {}

        self.ini["core"] = "tarantool"
        self.ini["module"] = "box"

        if os.access(suite_path, os.F_OK) == False:
            raise RuntimeError("Suite \"" + suite_path + \
                               "\" doesn't exist")

        # read the suite config
        config = ConfigParser.ConfigParser()
        config.read(os.path.join(suite_path, "suite.ini"))
        self.ini.update(dict(config.items("default")))
        self.ini["config"] = os.path.join(suite_path, self.ini["config"])

        if self.ini.has_key("init_lua"):
            self.ini["init_lua"] = os.path.join(suite_path,
                                                self.ini["init_lua"])
        else:
            self.ini["init_lua"] = None

        if self.ini.has_key("disabled"):
            self.ini["disabled"] = dict.fromkeys(self.ini["disabled"].split(" "))
        else:
            self.ini["disabled"] = dict()

        if self.ini.has_key("valgrind_disabled"):
            self.ini["valgrind_disabled"] = dict.fromkeys(self.ini["valgrind_disabled"].split(" "))
        else:
            self.ini["valgrind_disabled"] = dict()

        if self.ini.has_key("release_disabled"):
            self.ini["release_disabled"] = dict.fromkeys(self.ini["release_disabled"].split(" "))
        else:
            self.ini["release_disabled"] = dict()

        print "Collecting tests in \"" + suite_path + "\": " +\
            self.ini["description"] + "."

        for test_name in glob.glob(os.path.join(suite_path, "*.test")):
            for test_pattern in self.args.tests:
                if test_name.find(test_pattern) != -1:
                    self.tests.append(Test(test_name, self.args, self.ini))
        print "Found " + str(len(self.tests)) + " tests."

    def run_all(self):
        """For each file in the test suite, run client program
        assuming each file represents an individual test."""
        try:
            server = Server(self.ini["core"], self.ini["module"])
        except Exception as e:
            print e
            raise RuntimeError("Unknown server: core = {0}, module = {1}".format(
                               self.ini["core"], self.ini["module"]))

        if len(self.tests) == 0:
            # noting to test, exit
            return 0

        server.deploy(self.ini["config"],
                      server.find_exe(self.args.builddir, silent=False),
                      self.args.vardir, self.args.mem, self.args.start_and_exit,
                      self.args.gdb, self.args.valgrind,
                      init_lua=self.ini["init_lua"], silent=False)
        if self.args.start_and_exit:
            print "  Start and exit requested, exiting..."
            exit(0)

        longsep = "=============================================================================="
        shortsep = "------------------------------------------------------------"
        print longsep
        print string.ljust("TEST", 48), "RESULT"
        print shortsep
        failed_tests = []
        self.ini["server"] = server

        for test in self.tests:
            sys.stdout.write(string.ljust(test.name, 48))
            # for better diagnostics in case of a long-running test
            sys.stdout.flush()

            test_name = os.path.basename(test.name)
            if test_name in self.ini["disabled"]:
                print "[ skip ]"
            elif not server.debug and test_name in self.ini["release_disabled"]:
                print "[ skip ]"
            elif self.args.valgrind and test_name in self.ini["valgrind_disabled"]:
                print "[ skip ]"
            else:
                test.run(server)
                if not test.passed():
                    failed_tests.append(test.name)

        print shortsep
        if len(failed_tests):
            print "Failed {0} tests: {1}.".format(len(failed_tests),
                                                ", ".join(failed_tests))
        server.stop(silent=False)
        server.cleanup()

        if self.args.valgrind and check_valgrind_log(server.valgrind_log):
            print "  Error! There were warnings/errors in valgrind log file:"
            print_tail_n(server.valgrind_log, 20)
            return 1
        return len(failed_tests)

