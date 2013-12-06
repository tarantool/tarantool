import os
import re
import sys
import glob
import time
import yaml
import errno
import daemon
import socket
import signal
import shlex
import shutil
import traceback
import subprocess
import ConfigParser

from lib.server import Server
from lib.box_connection import BoxConnection
from lib.test_suite import FilteredStream, Test, check_libs
from lib.admin_connection import AdminConnection

from lib.preprocessor import State

try:
    import cStringIO as StringIO
except ImportError:
    import StringIO

check_libs()
import tarantool

def check_port(port):
    """Check if the port we're connecting to is available"""
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect(("localhost", port))
    except socket.error as e:
        return
    raise RuntimeError("The server is already running on port {0}".format(port))

def prepare_gdb(binary, args):
    """Prepare server startup arguments to run under gdb."""
    args = shlex.split('screen -dmS tnt-gdb gdb %s -ex \'b main\' -ex run' % binary) + args
    return args

def prepare_valgrind(args, valgrind_log, valgrind_sup):
    "Prepare server startup arguments to run under valgrind."
    args = [ "valgrind", "--log-file={0}".format(valgrind_log),
             "--suppressions={0}".format(valgrind_sup),
             "--gen-suppressions=all", "--show-reachable=yes", "--leak-check=full",
             "--read-var-info=yes", "--quiet" ] + args
    return args

def check_tmpfs_exists():
    return os.uname()[0] in 'Linux' and os.path.isdir("/dev/shm")

def create_tmpfs_vardir(vardir):
    os.makedirs(os.path.join("/dev/shm", vardir))
    os.symlink(os.path.join("/dev/shm", vardir), vardir)

class FuncTest(Test):
    def execute(self, server):
        execfile(self.name, dict(locals(), **server.__dict__))


class LuaTest(FuncTest):
    def execute(self, server):
        ts = State(self.suite_ini, server.admin, TarantoolServer)
        cmd = None

        def send_command(command):
            result = ts.curcon[0](command, silent=True)
            for conn in ts.curcon[1:]:
                conn(command, silent=True)
            return result

        for line in open(self.name, 'r'):
            if not cmd:
                cmd = StringIO.StringIO()
            if line.find('--#') == 0:
                rescom = cmd.getvalue().replace('\n\n', '\n')
                if rescom:
                    result = send_command(rescom)
                    sys.stdout.write(cmd.getvalue())
                    sys.stdout.write(result.replace("\r\n", "\n"))
                sys.stdout.write(line)
                ts(line)
            elif line.find('--') == 0:
                sys.stdout.write(line)
            else:
                if line.strip() or cmd.getvalue():
                    cmd.write(line)
                delim_len = -len(ts.delimiter) if len(ts.delimiter) else None
                if line.endswith(ts.delimiter+'\n') and cmd.getvalue().strip()[:delim_len].strip():
                    rescom = cmd.getvalue()[:delim_len].replace('\n\n', '\n')
                    result = send_command(rescom)
                    sys.stdout.write(cmd.getvalue())
                    sys.stdout.write(result.replace("\r\n", "\n"))
                    cmd.close()
                    cmd = None
        ts.flush()


class PythonTest(FuncTest):
    def execute(self, server):
        execfile(self.name, dict(locals(), **server.__dict__))


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
        return super(TarantoolServer, cls).__new__(cls)

    def __init__(self, core="tarantool"):
        Server.__init__(self, core)
        self.default_bin_name = "tarantool_box"
        self.default_config_name = "tarantool.cfg"
        self.default_init_lua_name = "init.lua"
        # append additional cleanup patterns
        self.re_vardir_cleanup += ['*.snap',
                                   '*.xlog',
                                   '*.inprogress',
                                   '*.cfg',
                                   '*.sup',
                                   '*.lua',
                                   '*.pid']
        self.process = None
        self.config = None
        self.vardir = None
        self.valgrind_log = "valgrind.log"
        self.valgrind_sup = os.path.join("share/", "%s.sup" % ('tarantool'))
        self.init_lua = None
        self.default_suppression_name = "valgrind.sup"
        self.pidfile = None
        self.port = None
        self.binary = None
        self.is_started = False
        self.mem = False
        self.start_and_exit = False
        self.gdb = False
        self.valgrind = False
        self.installed = False
        self.need_init = True

    def __del__(self):
        self.stop()

    def find_exe(self, builddir, silent=True):
        "Locate server executable in the build dir or in the PATH."
        self.builddir = builddir
        builddir = os.path.join(builddir, "src/box")
        path = builddir + os.pathsep + os.environ["PATH"]
        if not silent:
            print "Looking for server binary in {0} ...".format(path)
        for _dir in path.split(os.pathsep):
            exe = os.path.join(_dir, self.default_bin_name)
            if os.access(exe, os.X_OK):
                return exe
        raise RuntimeError("Can't find server executable in " + path)

    def install(self, binary=None, vardir=None, mem=None, silent=True):
        """Install server instance: create necessary directories and files.
        The server working directory is taken from 'vardir',
        specified in the program options."""

        if vardir != None: self.vardir = vardir
        if binary != None: self.binary = os.path.abspath(binary)
        if mem != None: self.mem = mem

        self.pidfile = os.path.abspath(os.path.join(self.vardir, self.pidfile))
        self.valgrind_log = os.path.abspath(os.path.join(self.vardir, self.valgrind_log))

        if not silent:
            print "Installing the server..."
            print "  Found executable at " + self.binary
            print "  Creating and populating working directory in " + self.vardir + "..."

        if os.access(self.vardir, os.F_OK):
            if not silent:
                print "  Found old vardir, deleting..."
            self.kill_old_server()
            self.cleanup()
        else:
            if (self.mem == True and check_tmpfs_exists() and
                os.path.basename(self.vardir) == self.vardir):
                create_tmpfs_vardir(self.vardir)
            else:
                os.makedirs(self.vardir)

        shutil.copy(self.config,
                    os.path.join(self.vardir, self.default_config_name))
        shutil.copy(self.valgrind_sup,
                    os.path.join(self.vardir, self.default_suppression_name))

        var_init_lua = os.path.join(self.vardir, self.default_init_lua_name)

        if self.init_lua != None:
            if os.path.exists(var_init_lua):
                os.remove(var_init_lua)
            shutil.copy(self.init_lua, var_init_lua)

        self.installed = True


    def configure(self, config):
        def get_option(config, section, key):
            if not config.has_option(section, key):
                return None
            value = config.get(section, key)
            if value.isdigit():
                value = int(value)
            return value
        self.config = os.path.abspath(config)
        # now read the server config, we need some properties from it
        with open(self.config) as fp:
            dummy_section_name = "tarantool"
            config = ConfigParser.ConfigParser()
            config.readfp(TarantoolConfigFile(fp, dummy_section_name))

            self.pidfile = get_option(config, dummy_section_name, "pid_file")
            self.primary_port = get_option(config, dummy_section_name, "primary_port")
            self.admin_port = get_option(config, dummy_section_name, "admin_port")

        self.port = self.admin_port
        self.admin = AdminConnection("localhost", self.admin_port)
        self.sql = BoxConnection("localhost", self.primary_port)

    def reconfigure(self, config, silent=False):
        if config == None:
            os.unlink(os.path.join(self.vardir, self.default_config_name))
        else:
            self.config = os.path.abspath(config)
            shutil.copy(self.config, os.path.join(self.vardir, self.default_config_name))
        self.admin.execute("box.cfg.reload()", silent=silent)

    def init(self):
        # init storage
        cmd = [self.binary, "--init-storage"]
        _init = subprocess.Popen(cmd, cwd=self.vardir,
                stderr = subprocess.STDOUT, stdout = subprocess.PIPE)
        retcode = _init.wait()
        if retcode:
            sys.stderr.write("tarantool_box --init-storage error: \n%s\n" %  _init.stdout.read())
            raise subprocess.CalledProcessError(retcode, cmd)

    def get_param(self, param):
        if param:
            data = yaml.load(self.admin("box.info." + param, silent=True))[0]
        else:
            data = yaml.load(self.admin("box.info", silent=True))
        return data

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
            with daemon.DaemonContext(working_directory = self.vardir):
                subprocess.check_call(args)
        else:
            if not self.gdb:
                args.append("--background")
            else:
                raise RuntimeError("'--gdb' and '--start-and-exit' can't be defined together")
            self.server = subprocess.Popen(args, cwd = self.vardir)
            self.server.wait()

    def start(self, start_and_exit=None, gdb=None, valgrind=None, silent=True):
        if start_and_exit != None: self.start_and_exit = start_and_exit
        if gdb != None: self.gdb = gdb
        if valgrind != None: self.valgrind = valgrind
        self.debug = self.test_debug()

        if self.is_started:
            if not silent:
                print "The server is already started."
            return

        if not silent:
            print "Starting the server..."
            version = self.version()
            print "Starting {0} {1}.".format(os.path.basename(self.binary), version)

        check_port(self.port)
        args = self.prepare_args()

        if self.gdb:
            args = prepare_gdb(self.binary, args)
            print "You started the server in gdb mode."
            print "To attach, use `screen -r tnt-gdb`"
        elif self.valgrind:
            args = prepare_valgrind([self.binary] + args, self.valgrind_log,
                                    os.path.abspath(os.path.join(self.vardir,
                                    self.default_suppression_name)))
        else:
            args = [self.binary] + args

        if self.start_and_exit:
            self._start_and_exit(args)
            return

        self.process = subprocess.Popen(args, cwd = self.vardir)

        # wait until the server is connected
        self.wait_until_started()
        # Set is_started flag, to nicely support cleanup during an exception.
        self.is_started = True


    def stop(self, silent=True):
        """Stop server instance. Do nothing if the server is not started,
        to properly shut down the server in case of an exception during
        start up."""
        if not self.is_started:
            if not silent:
                print "The server is not started."
            return

        if not silent:
            print "Stopping the server..."

        if self.process == None:
            self.kill_old_server()
            return

        # kill process
        pid = self.read_pidfile()
        if pid != -1:
            os.kill(pid, signal.SIGTERM)

#       self.process.terminate()
        if self.gdb or self.valgrind:
            while True:
                if self.process.poll() != None:
                    break
                time.sleep(1)
        else:
            self.process.wait()

        self.wait_until_stopped(pid)
        # clean-up processs flags
        self.is_started = False
        self.process = None

    def deploy(self, config=None, binary=None, vardir=None,
               mem=None, start_and_exit=None, gdb=None, valgrind=None,
               valgrind_sup=None, init_lua=None, silent=True, need_init=None):
        if config != None: self.config = config
        if binary != None: self.binary = binary
        if vardir != None: self.vardir = vardir
        if mem != None: self.mem = mem
        if start_and_exit != None: self.start_and_exit = start_and_exit
        if gdb != None: self.gdb = gdb
        if valgrind != None: self.valgrind = valgrind
        if need_init != None: self.need_init = need_init

        if init_lua != None:
            self.init_lua = os.path.abspath(init_lua)
        else:
            self.init_lua = None

        self.configure(self.config)
        self.install(self.binary, self.vardir, self.mem, silent)
        if self.need_init:
            self.init()
        self.start(self.start_and_exit, self.gdb, self.valgrind, silent)

    def restart(self):
        self.stop(silent=True)
        self.start(silent=True)

    def test_option_get(self, show, option_list_str):
        args = [self.binary] + option_list_str.split()
        if show:
            print " ".join([os.path.basename(self.binary)] + args[1:])
        output = subprocess.Popen(args,
                                  cwd = self.vardir,
                                  stdout = subprocess.PIPE,
                                  stderr = subprocess.STDOUT).stdout.read()
        return output

    def test_option(self, option_list_str):
        print self.test_option_get(True, option_list_str)

    def test_debug(self):
        output = self.test_option_get(False, "-V")
        if re.search("-Debug", output):
            return True
        return False

    def kill_old_server(self, silent=True):
        """Kill old server instance if it exists."""
        pid = self.read_pidfile()
        if pid == -1:
            return # Nothing to do

        if not silent:
            print "  Found old server, pid {0}, killing...".format(pid)

        try:
            os.kill(pid, signal.SIGTERM)
            while os.kill(pid, 0) != -1:
                time.sleep(0.001)
        except OSError:
            pass

    def read_pidfile(self):
        if os.access(self.pidfile, os.F_OK) == False:
            # file is inaccessible (not exist or permission denied)
            return -1

        pid = -1
        try:
            with open(self.pidfile) as f:
                pid = int(f.read())
        except:
            pass
        return pid

    def wait_until_started(self):
        """Wait until the server is started and accepting connections"""

        while self.read_pidfile() == -1:
            time.sleep(0.001)

        is_connected = False
        while not is_connected and not self.gdb:
            if self.process.poll():
                raise OSError("Can't start tarantool");
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect(("localhost", self.port))
                is_connected = True
                sock.close()
            except socket.error as e:
                time.sleep(0.001)

    def wait_until_stopped(self, pid):
        """Wait until the server is stoped and has closed sockets"""
        while True:
            try:
                time.sleep(0.001)
                os.kill(pid, 0)
                continue
            except OSError as err:
                if err.errno == errno.ESRCH:
                    break
                raise

    def find_tests(self, test_suite, suite_path):
        def patterned(test, patterns):
            answer = []
            for i in patterns:
                if test.name.find(i) != -1:
                    answer.append(test)
            return answer

        tests  = [PythonTest(k, test_suite.args, test_suite.ini) for k in sorted(glob.glob(os.path.join(suite_path, "*.test.py" )))]
        tests += [LuaTest(k, test_suite.args, test_suite.ini)    for k in sorted(glob.glob(os.path.join(suite_path, "*.test.lua")))]
        test_suite.tests = sum(map((lambda x: patterned(x, test_suite.args.tests)), tests), [])
