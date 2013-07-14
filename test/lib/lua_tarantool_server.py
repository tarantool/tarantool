import os
import sys
import glob
from pprint import pprint

from server import Server
from test_suite import Test
from tarantool_server import TarantoolServer, FuncTest

class LuaTest(FuncTest):
    def execute(self, server):
        pprint('test lua',sys.stderr)
        for i in open(self.name, 'r').read().replace('\n\n', '\n').split(';\n'):
             server.admin(i)

class LuaTarantoolServer(TarantoolServer): 
    def __new__(cls, core="lua tarantool"):
        return TarantoolServer.__new__(cls)

    def __init__(self, core="lua tarantool"):
        TarantoolServer.__init__(self, core)

    def find_tests(self, test_suite, suite_path):
        for test_name in sorted(glob.glob(os.path.join(suite_path, "*.test"))):
            for test_pattern in test_suite.args.tests:
                if test_name.find(test_pattern) != -1:
                    test_suite.tests.append(LuaTest(test_name, test_suite.args, test_suite.ini))

