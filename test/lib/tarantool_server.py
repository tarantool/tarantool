import os
import sys
import glob
import time
import yaml
import shutil
import pexpect
import traceback
import subprocess
import ConfigParser
import pprint

import tarantool_preprocessor

from server import Server
from box_connection import BoxConnection
from test_suite import FilteredStream, Test
from admin_connection import AdminConnection
from memcached_connection import MemcachedConnection


class FuncTest(Test):
    def __init__(self, name, args, suite_ini):
        Test.__init__(self, name, args, suite_ini)
        self.name = name
        self.result = name.replace(".test", ".result")
        self.skip_cond = name.replace(".test", ".skipcond")
        self.tmp_result = os.path.join(self.args.vardir,
                os.path.basename(self.result))
        self.reject = "{0}/test/{1}".format(self.args.builddir, 
                name.replace(".test", ".reject"))

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
        
    def __repr__(self):
        return str([self.name, self.result, self.skip_cond, self.tmp_result,
        self.reject])

    __str__ = __repr__ 

    
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
        # tarantool.cfg puts string values in quote
        return self.fp.readline().replace("\"", '')

class TarantoolServer(Server):
    def __new__(cls, core="tarantool"):
        return super(Server, cls).__new__(cls)

    def __init__(self, core="tarantool"):
        Server.__init__(self, core)
        self.default_config_name = "tarantool.cfg"
        self.default_init_lua_name = "init.lua"
        # append additional cleanup patterns
        self.re_vardir_cleanup += ['*.snap',
                                   '*.xlog',
                                   '*.inprogress',
                                   '*.cfg',
                                   '*.sup',
                                   '*.lua']

    def find_exe(self, builddir, silent=True):
        return Server.find_exe(self, "{0}/src/box/".format(builddir), silent)

    def configure(self, config):
        Server.configure(self, config)
        # now read the server config, we need some properties from it
        with open(self.config) as fp:
            dummy_section_name = "tarantool"
            config = ConfigParser.ConfigParser()
            config.readfp(TarantoolConfigFile(fp, dummy_section_name))

            self.pidfile = config.get(dummy_section_name, "pid_file")
            self.primary_port = self.get_option_int(config, dummy_section_name, "primary_port")
            self.admin_port = self.get_option_int(config, dummy_section_name, "admin_port")
            self.memcached_port = self.get_option_int(config, dummy_section_name, "memcached_port")

        self.port = self.admin_port
        self.admin = AdminConnection("localhost", self.admin_port)
        self.sql = BoxConnection("localhost", self.primary_port)
        if self.memcached_port != 0:
            # Run memcached client
            self.memcached = MemcachedConnection('localhost', self.memcached_port)

    def find_tests(self, test_suite, suite_path):
        for test_name in glob.glob(os.path.join(suite_path, "*.test")):
            for test_pattern in test_suite.args.tests:
                if test_name.find(test_pattern) != -1:
                    test_suite.tests.append(FuncTest(test_name, test_suite.args, test_suite.ini))

    def reconfigure(self, config, silent=False):
        if config == None:
            os.unlink(os.path.join(self.vardir, self.default_config_name))
        else:
            self.config = os.path.abspath(config)
            shutil.copy(self.config, os.path.join(self.vardir, self.default_config_name))
        self.admin.execute("reload configuration", silent=silent)

    def get_option_int(self, config, section, option):
        if config.has_option(section, option):
            return config.getint(section, option)
        else:
            return 0

    def init(self):
        # init storage
        subprocess.check_call([self.binary, "--init-storage"],
                              cwd = self.vardir,
                              # catch stdout/stderr to not clutter output
                              stdout = subprocess.PIPE,
                              stderr = subprocess.PIPE)

    def get_param(self, param):
        data = self.admin.execute("show info", silent = True)
        info = yaml.load(data)["info"]
        return info[param]

    def wait_lsn(self, lsn):
        while True:
            curr_lsn = int(self.get_param("lsn"))
            if (curr_lsn >= lsn):
                break
            time.sleep(0.01)
    
    def version(self):
        p = subprocess.Popen([self.binary, "--version"],
                             cwd = self.vardir,
                             stdout = subprocess.PIPE)
        version = p.stdout.read().rstrip()
        p.wait()
        return version

    def _start_and_exit(self, args, gdb=None, valgrind=None):
        if gdb != None: self.gdb = gdb
        if valgrind != None: self.valgrind = valgrind

        if self.valgrind:
            Server._start_and_exit(self, args)
        else:
            if not self.gdb:
                args.append("--background")
            else:
                raise RuntimeError("'--gdb' and '--start-and-exit' can't be defined together")
            self.server = pexpect.spawn(args[0], args[1:], cwd = self.vardir)
            self.server.wait()

    def default_bin_name(self):
        return "{0}".format(self.core) 
