import os
import sys
import glob

from server import Server
from test_suite import Test
from tarantool_server import TarantoolServer, FuncTest

class PythonTest(FuncTest):
    def execute(self, server):
        execfile(self.name, dict(locals(), **server.__dict__))

class PythonTarantoolServer(TarantoolServer):
    def __new__(cls, core="python tarantool"):
        return TarantoolServer.__new__(cls)

    def __init__(self, core="python tarantool"):
        TarantoolServer.__init__(self, core)

    def find_tests(self, test_suite, suite_path):
        for test_name in sorted(glob.glob(os.path.join(suite_path, "*.test"))):
            for test_pattern in test_suite.args.tests:
                if test_name.find(test_pattern) != -1:
                    test_suite.tests.append(PythonTest(test_name, test_suite.args, test_suite.ini))

