from server import Server
import subprocess
import sys
import os

class UnittestServer(Server):
    """A dummy server implementation for unit test suite"""
    def __new__(cls, core="unittest", module="dummy"):
        return Server.__new__(cls)


    def __init__(self, core="unittest", module="dummy"):
        Server.__init__(self, core, module)
        self.debug = False

    def configure(self, config):
        pass
    def deploy(self, config=None, binary=None, vardir=None,
               mem=None, start_and_exit=None, gdb=None, valgrind=None,
               valgrind_sup=None, init_lua=None, silent=True, need_init=True):
        self.vardir = vardir
        def run_test(name):
            p = subprocess.Popen([os.path.join(self.builddir, "test/unit", name)], stdout=subprocess.PIPE)
            p.wait()
            for line in p.stdout.readlines():
                sys.stdout.write(line)

        self.run_test = run_test

    def start(self):
        pass
    def find_exe(self, builddir, silent=False):
        self.builddir = builddir

    def init(self):
        pass
