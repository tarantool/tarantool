import os
import re
import sys
import time
import shutil
import difflib
import filecmp
import threading
import traceback
import collections
import ConfigParser

from lib.server import Server
from lib.colorer import Colorer

color_stdout = Colorer()
try:
    from cStringIO import StringIO
except ImportError:
    from StringIO import StringIO

def check_libs():
    deps = [
        ('msgpack', 'msgpack-python'),
        ('tarantool', 'tarantool-python/src')
    ]
    base_path = os.path.dirname(os.path.abspath(__file__))

    for (mod_name, mod_dir) in deps:
        mod_path = os.path.join(base_path, mod_dir)
        if mod_path not in sys.path:
            sys.path = [mod_path] + sys.path

    for (mod_name, _mod_dir) in deps:
        try:
            __import__(mod_name)
        except ImportError as e:
            color_stdout("\n\nNo %s library found\n" % mod_name, schema='error')
            print(e)
            sys.exit(1)


class FilteredStream:
    """Helper class to filter .result file output"""
    def __init__(self, filename):
        self.stream = open(filename, "w+")
        self.filters = []

    def write(self, fragment):
        """Apply all filters, then write result to the undelrying stream.
        Do line-oriented filtering: the fragment doesn't have to represent
        just one line."""
        fragment_stream = StringIO(fragment)
        skipped = False
        for line in fragment_stream:
            original_len = len(line.strip())
            for pattern, replacement in self.filters:
                line = re.sub(pattern, replacement, line)
                # don't write lines that are completely filtered out:
                skipped = original_len and not line.strip()
                if skipped:
                    break
            if not skipped:
                self.stream.write(line)

    def push_filter(self, pattern, replacement):
        self.filters.append([pattern, replacement])

    def pop_filter(self):
        self.filters.pop()

    def clear_all_filters(self):
        self.filters = []

    def close(self):
        self.clear_all_filters()
        self.stream.close()

    def flush(self):
        self.stream.flush()


def check_valgrind_log(path_to_log):
    """ Check that there were no warnings in the log."""
    return os.path.exists(path_to_log) and os.path.getsize(path_to_log) != 0


def print_tail_n(filename, num_lines):
    """Print N last lines of a file."""
    with open(filename, "r+") as logfile:
        tail_n = collections.deque(logfile, num_lines)
        for line in tail_n:
            color_stdout(line, schema='tail')


class Test:
    """An individual test file. A test object can run itself
    and remembers completion state of the run.

    If file <test_name>.skipcond is exists it will be executed before
    test and if it sets self.skip to True value the test will be skipped.
    """

    def __init__(self, name, args, suite_ini):
        """Initialize test properties: path to test file, path to
        temporary result file, path to the client program, test status."""
        rg = re.compile('.test.*')
        self.name = name
        self.args = args
        self.suite_ini = suite_ini
        self.result = rg.sub('.result', name)
        self.skip_cond = rg.sub('.skipcond', name)
        self.tmp_result = os.path.join(self.args.vardir,
                                       os.path.basename(self.result))
        self.reject = rg.sub('.reject', name)
        self.is_executed = False
        self.is_executed_ok = None
        self.is_equal_result = None
        self.is_valgrind_clean = True
        self.is_terminated = False

    def passed(self):
        """Return true if this test was run successfully."""
        return self.is_executed and self.is_executed_ok and self.is_equal_result

    def execute(self):
        pass

    def run(self, server):
        """Execute the test assuming it's a python program.
        If the test aborts, print its output to stdout, and raise
        an exception. Else, comprare result and reject files.
        If there is a difference, print it to stdout and raise an
        exception. The exception is raised only if is_force flag is
        not set."""
        diagnostics = "unknown"
        save_stdout = sys.stdout
        try:
            self.skip = False
            if os.path.exists(self.skip_cond):
                sys.stdout = FilteredStream(self.tmp_result)
                stdout_fileno = sys.stdout.stream.fileno()
                execfile(self.skip_cond, dict(locals(), **server.__dict__))
                sys.stdout.close()
                sys.stdout = save_stdout
            if not self.skip:
                sys.stdout = FilteredStream(self.tmp_result)
                stdout_fileno = sys.stdout.stream.fileno()
                temp = threading.Thread(target=self.execute, args=(server, ))
                temp.start()
                temp.join(self.suite_ini["timeout"])
                if temp.is_alive():
                    temp._Thread__stop()
                    self.is_terminated = True
                sys.stdout.flush()
            self.is_executed_ok = True
        except Exception as e:
            traceback.print_exc(e)
            diagnostics = str(e)
        finally:
            if sys.stdout and sys.stdout != save_stdout:
                sys.stdout.close()
            sys.stdout = save_stdout
        self.is_executed = True
        sys.stdout.flush()

        if not self.skip:
            if self.is_executed_ok and os.path.isfile(self.result):
                self.is_equal_result = filecmp.cmp(self.result, self.tmp_result)
        else:
            self.is_equal_result = 1

        if self.args.valgrind:
            self.is_valgrind_clean = \
            check_valgrind_log(server.valgrind_log) == False

        elif self.skip:
            color_stdout("[ skip ]\n", schema='test_skip')
            if os.path.exists(self.tmp_result):
                os.remove(self.tmp_result)
        elif self.is_executed_ok and self.is_equal_result and self.is_valgrind_clean:
            color_stdout("[ pass ]\n", schema='test_pass')
            if os.path.exists(self.tmp_result):
                os.remove(self.tmp_result)
        elif (self.is_executed_ok and not self.is_equal_result and not
              os.path.isfile(self.result)):
            os.rename(self.tmp_result, self.result)
            color_stdout("[ new ]\n", schema='test_new')
        else:
            os.rename(self.tmp_result, self.reject)
            color_stdout("[ fail ]\n", schema='test_fail')

            where = ""
            if not self.is_executed_ok:
                self.print_diagnostics(self.reject, "Test failed! Last 10 lines of the result file:")
                server.print_log(15)
                where = ": test execution aborted, reason '{0}'".format(diagnostics)
            elif not self.is_equal_result:
                self.print_unidiff()
                server.print_log(15)
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

        color_stdout(message, schema='error')
        print_tail_n(logfile, 10)

    def print_unidiff(self):
        """Print a unified diff between .test and .result files. Used
        to establish the cause of a failure when .test differs
        from .result."""

        color_stdout("\nTest failed! Result content mismatch:\n", schema='error')
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

                color_stdout.writeout_unidiff(diff)

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
        self.suite_path = suite_path
        self.ini["core"] = "tarantool"

        if os.access(suite_path, os.F_OK) == False:
            raise RuntimeError("Suite \"" + suite_path + \
                               "\" doesn't exist")

        # read the suite config
        config = ConfigParser.ConfigParser()
        config.read(os.path.join(suite_path, "suite.ini"))
        self.ini.update(dict(config.items("default")))

        for i in ["config", "init_lua"]:
            self.ini[i] = os.path.join(suite_path, self.ini[i]) if i in self.ini else None
        for i in ["disabled", "valgrind_disabled", "release_disabled"]:
            self.ini[i] = dict.fromkeys(self.ini[i].split()) if i in self.ini else dict()
        for i in ["lua_libs"]:
            self.ini[i] = map(lambda x: os.path.join(suite_path, x),
                    dict.fromkeys(self.ini[i].split()) if i in self.ini else
                    dict())
        for i in ["timeout"]:
            self.ini[i] = int(self.ini[i]) if i in self.ini else 10

        try:
            self.server = Server(self.ini["core"])
            self.ini["server"] = self.server
        except Exception as e:
            print e
            raise RuntimeError("Unknown server: core = {0}".format(
                               self.ini["core"]))

        color_stdout("Collecting tests in ", schema='ts_text')
        color_stdout(repr(suite_path), schema='path')
        color_stdout(": ", self.ini["description"], ".\n", schema='ts_text')
        self.server.find_tests(self, suite_path)
        color_stdout("Found ", str(len(self.tests)), " tests.\n", schema='path')

    def run_all(self):
        """For each file in the test suite, run client program
        assuming each file represents an individual test."""

        if not self.tests:
            # noting to test, exit
            return []

        self.server.deploy(self.ini["config"],
                      self.server.find_exe(self.args.builddir, silent=False),
                      self.args.vardir, self.args.mem, self.args.start_and_exit,
                      self.args.gdb, self.args.valgrind,
                      init_lua=self.ini["init_lua"], silent=False)
        if self.ini['core'] != 'unittest':
            self.ini['servers'] = {'default' : self.server}
            self.ini['connections'] = {'default' : [self.server.admin, 'default']}
            self.ini['vardir'] = self.args.vardir
            self.ini['builddir'] = self.args.builddir
        for i in self.ini['lua_libs']:
            shutil.copy(i, self.args.vardir)

        if self.args.start_and_exit:
            color_stdout("    Start and exit requested, exiting...\n", schema='info')
            exit(0)

        longsep = '='*70
        shortsep = '-'*60
        color_stdout(longsep, "\n", schema='separator')
        color_stdout("TEST".ljust(48), schema='t_name')
        color_stdout("RESULT\n", schema='test_pass')
        color_stdout(shortsep, "\n", schema='separator')
        failed_tests = []
        try:
            for test in self.tests:
                color_stdout(test.name.ljust(48), schema='t_name')
                # for better diagnostics in case of a long-running test

                test_name = os.path.basename(test.name)

                if (test_name in self.ini["disabled"]
                    or not self.server.debug and test_name in self.ini["release_disabled"]
                    or self.args.valgrind and test_name in self.ini["valgrind_disabled"]):
                    color_stdout("[ disabled ]\n", schema='t_name')
                else:
                    test.run(self.server)
                    if not test.passed():
                        failed_tests.append(test.name)
            self.server.stop(silent=False)
            self.server.cleanup()
        except (KeyboardInterrupt) as e:
            self.server.stop(silent=False)
            self.server.cleanup()
            color_stdout('\n')
            raise
        finally:
            color_stdout(shortsep, "\n", schema='separator')

        if failed_tests:
            color_stdout("Failed {0} tests: {1}.".format(len(failed_tests),
                                                ", ".join(failed_tests)),
                                                schema='error')

        if self.args.valgrind and check_valgrind_log(self.server.valgrind_log):
            color_stdout("  Error! There were warnings/errors in valgrind log file:", schema='error')
            print_tail_n(self.server.valgrind_log, 20)
            return ['valgrind error in ' + self.suite_path]
        return failed_tests

