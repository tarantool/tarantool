import os
import stat
import shutil
import subprocess
import pexpect
import socket
import sys
import signal
import time
import daemon
import glob
import ConfigParser
import re

def check_port(port):
    """Check if the port we're connecting to is available"""

    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect(("localhost", port))
    except socket.error as e:
        return
    raise RuntimeError("The server is already running on port {0}".format(port))

def prepare_gdb(args):
    """Prepare server startup arguments to run under gdb."""

    if "TERM" in os.environ:
        term = os.environ["TERM"]
    else:
        term = "xterm"

    if term not in ["xterm", "rxvt", "urxvt", "gnome-terminal", "konsole"]:
        raise RuntimeError("--gdb: unsupported terminal {0}".format(term))

    args = [ term, "-e", "gdb", "-ex", "break main", "-ex", "run" ] + args
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

class Server(object):
    """Server represents a single server instance. Normally, the
    program operates with only one server, but in future we may add
    replication slaves. The server is started once at the beginning
    of each suite, and stopped at the end."""

    def __new__(cls, core=None, module=None):
        if core  == None:
            return super(Server, cls).__new__(cls)
        mdlname = "lib.{0}_server".format(core)
        clsname = "{0}Server".format(core.title())
        corecls = __import__(mdlname, fromlist=clsname).__dict__[clsname]
        return corecls.__new__(corecls, core, module)

    def __init__(self, core, module):
        self.core = core
        self.module = module
        self.re_vardir_cleanup = ['*.core.*', 'core']
        self.process = None
        self.default_config_name = None
        self.default_init_lua_name = None
        self.config = None
        self.vardir = None
        self.valgrind_log = "valgrind.log"
        self.valgrind_sup = os.path.join("share/", "%s_%s.sup" % (core, module))
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

    def find_exe(self, builddir, silent=True):
        "Locate server executable in the build dir or in the PATH."
        exe_name = self.default_bin_name()
        path = builddir + os.pathsep + os.environ["PATH"]

        if not silent:
            print "  Looking for server binary in {0} ...".format(path)

        for dir in path.split(os.pathsep):
            exe = os.path.join(dir, exe_name)
            if os.access(exe, os.X_OK):
                return exe

        raise RuntimeError("Can't find server executable in " + path)

    def cleanup(self, full=False):
        trash = []

        for re in self.re_vardir_cleanup:
            trash += glob.glob(os.path.join(self.vardir, re))

        for filename in trash:
            os.remove(filename)

        if full:
            shutil.rmtree(self.vardir)

    def configure(self, config):
        self.config = os.path.abspath(config)

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

        shutil.copy(self.config, os.path.join(self.vardir,
                                              self.default_config_name))
        shutil.copy(self.valgrind_sup,
                    os.path.join(self.vardir, self.default_suppression_name))

        var_init_lua = os.path.join(self.vardir, self.default_init_lua_name)
        if self.init_lua != None:
            shutil.copy(self.init_lua, var_init_lua)
        elif os.path.exists(var_init_lua):
            # We must delete old init.lua if it exists
            os.remove(var_init_lua)

    def init(self):
        pass

    def _start_and_exit(self, args, gdb=None, valgrind=None):
        if gdb != None: self.gdb = gdb
        if valgrind != None: self.valgrind = valgrind

        if self.gdb == True:
            raise RuntimeError("'--gdb' and '--start-and-exit' can't be defined together")
        with daemon.DaemonContext(working_directory = self.vardir):
            os.execvp(args[0], args)

    def prepare_args(self):
        return [self.binary]

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
            args = prepare_gdb(args)
        elif self.valgrind:
            args = prepare_valgrind(args, self.valgrind_log,
                                    os.path.abspath(os.path.join(self.vardir,
                                    self.default_suppression_name)))

        if self.start_and_exit:
            self._start_and_exit(args)
            return

        self.process = pexpect.spawn(args[0], args[1:], cwd = self.vardir)

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
        os.kill(self.read_pidfile(), signal.SIGTERM)
        #self.process.kill(signal.SIGTERM)
        if self.gdb or self.valgrind:
            self.process.expect(pexpect.EOF, timeout = 1 << 30)
        else:
            self.process.expect(pexpect.EOF)
        self.process.close()

        self.wait_until_stopped()
        # clean-up processs flags
        self.is_started = False
        self.process = None

    def deploy(self, config=None, binary=None, vardir=None,
               mem=None, start_and_exit=None, gdb=None, valgrind=None,
               valgrind_sup=None, init_lua=None, silent=True, need_init=True):
        if config != None: self.config = config
        if binary != None: self.binary = binary
        if vardir != None: self.vardir = vardir
        if mem != None: self.mem = mem
        if start_and_exit != None: self.start_and_exit = start_and_exit
        if gdb != None: self.gdb = gdb
        if valgrind != None: self.valgrind = valgrind

        if init_lua != None:
            self.init_lua = os.path.abspath(init_lua)
        else:
            self.init_lua = None;

        self.configure(self.config)
        self.install(self.binary, self.vardir, self.mem, silent)
        if need_init:
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
        while not is_connected:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect(("localhost", self.port))
                is_connected = True
                sock.close()
            except socket.error as e:
                time.sleep(0.001)

    def wait_until_stopped(self):
        """Wait until the server is stoped and has closed sockets"""

        while self.read_pidfile() != -1:
            time.sleep(0.001)

        is_connected = False
        while not is_connected:
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.connect(("localhost", self.port))
                is_connected = True
                sock.close()
                time.sleep(0.001)
                continue
            except socket.error as e:
                break

