import os
import re
import sys
import traceback
import subprocess

from subprocess import Popen, PIPE

import tarantool_preprocessor

from server import Server
from test_suite import FilteredStream, Test


class UnitTest(Test):
    def __init__(self, name, args, suite_ini):
        Test.__init__(self, name, args, suite_ini)
        self.name = name
        self.result = name + ".result"
        self.skip_cond = name + ".skipcond"
        self.tmp_result = os.path.join(self.args.vardir,
                                       os.path.basename(self.result))
        self.reject = "{0}/test/{1}".format(self.args.builddir, name + ".reject")

    def execute(self, server):
        diagnostics = "unknown"
        builddir = self.args.builddir
        save_stdout = sys.stdout
        try:
            self.skip = 0
            if os.path.exists(self.skip_cond):
                sys.stdout = FilteredStream(self.tmp_result)
                stdout_fileno = sys.stdout.stream.fileno()
                execfile(self.skip_cond, dict(locals(), **server.__dict__))
                sys.stdout.close()
                sys.stdout = save_stdout
            if not self.skip:
                sys.stdout = FilteredStream(self.tmp_result)
                stdout_fileno = sys.stdout.stream.fileno()
                execs = [os.path.join(server.builddir, "test", self.name)]
                proc = Popen(execs, stdout=PIPE)
                sys.stdout.write(proc.communicate()[0])
            self.is_executed_ok = True
        except Exception as e:
            traceback.print_exc(e)
            diagnostics = str(e)
        finally:
            if sys.stdout and sys.stdout != save_stdout:
                sys.stdout.close()
            sys.stdout = save_stdout; 
        self.is_executed = True

    def __repr__(self):
        return str([self.name, self.result, self.skip_cond, self.tmp_result,
        self.reject])

    __str__ = __repr__ 


class UnittestServer(Server):
    """A dummy server implementation for unit test suite"""
    def __new__(cls, core="unittest"):
        return Server.__new__(cls)


    def __init__(self, core="unittest"):
        Server.__init__(self, core)
        self.debug = False

    def configure(self, config):
        pass
    def deploy(self, config=None, binary=None, vardir=None,
               mem=None, start_and_exit=None, gdb=None, valgrind=None,
               valgrind_sup=None, init_lua=None, silent=True, need_init=True):
        self.vardir = vardir
        def run_test(name):
            p = subprocess.Popen([os.path.join(self.builddir, "test/", name)], stdout=subprocess.PIPE)
            p.wait()
            for line in p.stdout.readlines():
                sys.stdout.write(line)

        self.run_test = run_test
        if not os.access(self.vardir, os.F_OK):
            if (self.mem == True and check_tmpfs_exists() and
                os.path.basename(self.vardir) == self.vardir):
                create_tmpfs_vardir(self.vardir)
            else:
                os.makedirs(self.vardir)


    def start(self):
        pass
    def find_exe(self, builddir, silent=False):
        self.builddir = builddir

    def find_tests(self, test_suite, suite_path):
        def patterned(name):
            for i in test_suite.args.tests:
                if name.find(i) != -1:
                    return True
            return False

        regexp = re.compile('[a-zA-Z0-9_]*')
        test_suite.tests.extend([UnitTest(os.path.join(suite_path, test), test_suite.args, test_suite.ini) 
                for test in os.listdir(suite_path) if 
                    regexp.match(test) and
                    os.access(os.path.join(suite_path, test), os.X_OK) and
                    os.path.isfile(os.path.join(suite_path, test)) and
                    patterned(test)]) 
        print "Found " + str(len(test_suite.tests)) + " tests."

    def init(self):
        pass
