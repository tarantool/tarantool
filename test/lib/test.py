import os
import re
import sys
import time
import filecmp
import difflib
import traceback

try:
    from cStringIO import StringIO
except ImportError:
    from StringIO import StringIO

from lib.colorer import Colorer
from lib.utils import check_valgrind_log, print_tail_n
color_stdout = Colorer()

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

    def execute(self, server):
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
                self.execute(server)
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
            self.is_valgrind_clean = (check_valgrind_log(server.valgrind_log) == False)

        if self.skip:
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
                self.print_diagnostics(self.reject, "Test failed! Last 10 lines of the result file:\n")
                server.print_log(15)
                where = ": test execution aborted, reason '{0}'".format(diagnostics)
            elif not self.is_equal_result:
                self.print_unidiff()
                server.print_log(15)
                where = ": wrong test output"
            elif not self.is_valgrind_clean:
                os.remove(self.reject)
                self.print_diagnostics(server.valgrind_log, "Test failed! Last 10 lines of valgrind.log:\n")
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


