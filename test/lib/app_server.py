import os
import re
import sys
import glob
import traceback
import subprocess
from subprocess import Popen, PIPE

from lib.server import Server
from lib.tarantool_server import Test

class AppTest(Test):
    def execute(self, server):
        execs = [os.path.join(os.path.abspath(server.builddir), "test", self.name)]
        proc = Popen(execs, stdout=PIPE, cwd=server.vardir)
        sys.stdout.write(proc.communicate()[0])

class AppServer(Server):
    """A dummy server implementation for application server tests"""
    def __new__(cls, ini=None):
        return Server.__new__(cls)

    def __init__(self, _ini=None):
        if _ini is None:
            _ini = {}
        ini = {
            'vardir': None
        }; ini.update(_ini)
        Server.__init__(self, ini)
        self.vardir = ini['vardir']
        self.builddir = ini['builddir']
        self.debug = False

    def deploy(self, config=None, binary=None, vardir=None,
               mem=None, start_and_exit=None, gdb=None, valgrind=None,
               init_lua=None, silent=True, need_init=True):
        self.vardir = vardir
        if not os.access(self.vardir, os.F_OK):
            os.makedirs(self.vardir)

    @classmethod
    def find_exe(cls, builddir):
        cls.builddir = builddir

    def find_tests(self, test_suite, suite_path):
        def patterned(test, patterns):
            answer = []
            for i in patterns:
                if test.name.find(i) != -1:
                    answer.append(test)
            return answer

        test_suite.tests = [AppTest(k, test_suite.args, test_suite.ini) for
    k in sorted(glob.glob(os.path.join(suite_path, "*.test.lua" )))]
        test_suite.tests = sum(map((lambda x: patterned(x, test_suite.args.tests)), test_suite.tests), [])

    def print_log(self, lines):
        pass
