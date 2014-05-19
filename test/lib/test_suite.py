import os
import re
import sys
import time
import shutil
import difflib
import threading
import traceback
import collections
import ConfigParser


from lib.tarantool_server import TarantoolServer
from lib.server import Server
from lib.colorer import Colorer
from lib.utils import check_valgrind_log, print_tail_n

color_stdout = Colorer()
try:
    from cStringIO import StringIO
except ImportError:
    from StringIO import StringIO

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
        self.ini.update(self.args.__dict__)

        for i in ["script"]:
            self.ini[i] = os.path.join(suite_path, self.ini[i]) if i in self.ini else None
        for i in ["disabled", "valgrind_disabled", "release_disabled"]:
            self.ini[i] = dict.fromkeys(self.ini[i].split()) if i in self.ini else dict()
        for i in ["lua_libs"]:
            self.ini[i] = map(lambda x: os.path.join(suite_path, x),
                    dict.fromkeys(self.ini[i].split()) if i in self.ini else
                    dict())
        try:
            if self.ini['core'] == 'tarantool':
                self.server = TarantoolServer(self.ini)
            else:
                self.server = Server(self.ini)
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
        self.server.deploy(silent=False)

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
            color_stdout(shortsep, "\n", schema='separator')
            self.server.stop(silent=False)
            self.server.cleanup()
        except (KeyboardInterrupt) as e:
            color_stdout('\n')
            color_stdout(shortsep, "\n", schema='separator')
            self.server.stop(silent=False)
            self.server.cleanup()
            raise

        if failed_tests:
            color_stdout("Failed {0} tests: {1}.\n".format(len(failed_tests),
                                                ", ".join(failed_tests)),
                                                schema='error')

        if self.args.valgrind and check_valgrind_log(self.server.valgrind_log):
            color_stdout(shortsep, "\n", schema='separator')
            color_stdout("  Error! There were warnings/errors in valgrind log file:\n", schema='error')
            print_tail_n(self.server.valgrind_log, 20)
            color_stdout(shortsep, "\n", schema='separator')
            return ['valgrind error in ' + self.suite_path]
        return failed_tests

