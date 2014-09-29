import os
import re
import sys
import glob
import stat
import time
import shlex
import daemon
import shutil
import signal
import socket
import subprocess
import ConfigParser

def check_port(port):
    """Check if the port we're connecting to is available"""
    try:
        sock = socket.create_connection(("localhost", port))
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

class Server(object):
    """Server represents a single server instance. Normally, the
    program operates with only one server, but in future we may add
    replication slaves. The server is started once at the beginning
    of each suite, and stopped at the end."""

    @property
    def vardir(self):
        if not hasattr(self, '_vardir'):
            raise ValueError("No vardir specified")
        return self._vardir
    @vardir.setter
    def vardir(self, path):
        if path == None:
            return
        self._vardir = os.path.abspath(path)


    def __new__(cls, ini=None):
        if ini == None or 'core' not in ini or ini['core'] is None:
            return object.__new__(cls)
        core = ini['core'].lower().strip()
        cls.mdlname = "lib.{0}_server".format(core.replace(' ', '_'))
        cls.clsname = "{0}Server".format(core.title().replace(' ', ''))
        corecls = __import__(cls.mdlname, fromlist=cls.clsname).__dict__[cls.clsname]
        return corecls.__new__(corecls, core)

    def __init__(self, ini):
        self.core = ini['core']
        self.ini = ini
        self.re_vardir_cleanup = ['*.core.*', 'core']
        self.vardir = ini['vardir']

    def prepare_args(self):
        return []

    def cleanup(self, full=False):
        if full:
            shutil.rmtree(self.vardir)
            return
        for re in self.re_vardir_cleanup:
            for f in glob.glob(os.path.join(self.vardir, re)):
                os.remove(f)

    def install(self, binary=None, vardir=None, mem=None, silent=True):
        pass
    def init(self):
        pass
    def start(self, silent=True):
        pass
    def stop(self, silent=True):
        pass
    def restart(self):
        pass
