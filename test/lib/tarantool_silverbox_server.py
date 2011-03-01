import os
import stat
import shutil
import subprocess
import pexpect
import sys
import signal
import time
import socket
import daemon
import glob

def wait_until_connected(host, port):
  """Wait until the server is started and accepting connections"""
  is_connected = False
  while not is_connected:
    try:
      sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
      sock.connect((host, port))
      is_connected = True
      sock.close()
    except socket.error as e:
      time.sleep(0.001)


def prepare_gdb(args):
  """Prepare server startup arguments to run under gdb."""
  if "TERM" in os.environ:
    term = os.environ["TERM"]
  else:
    term = "xterm"

  if term not in ["xterm", "rxvt", "urxvt", "gnome-terminal", "konsole"]:
    raise RuntimeError("--gdb: unsupported terminal {0}".format(term))

  args = [ term, "-e ", "gdb", "-ex", "break main", "-ex", "run"] + args
  return args


def prepare_valgrind(args, valgrind_opts):
  "Prepare server startup arguments to run under valgrind."
  args = ([ "valgrind", "--log-file=valgrind.log", "--quiet" ] +
          valgrind_opts.split(" ") + args)
  return args


def check_tmpfs_exists():
  return os.uname()[0] in 'Linux' and os.path.isdir("/dev/shm")

def create_tmpfs_vardir(vardir):
  os.mkdir(os.path.join("/dev/shm", vardir))
  os.symlink(os.path.join("/dev/shm", vardir), vardir)

class TarantoolSilverboxServer:
  """Server represents a single server instance. Normally, the
  program operates with only one server, but in future we may add
  replication slaves. The server is started once at the beginning
  of each suite, and stopped at the end."""

  def __init__(self, args, suite_ini):
    """Set server options: path to configuration file, pid file, exe, etc."""
    self.args = args
    self.suite_ini = suite_ini
    self.path_to_pidfile = os.path.join(args.vardir, suite_ini["pidfile"])
    self.path_to_exe = None
    self.abspath_to_exe = None
    self.is_started = False

  def install(self, silent = False):
    """Start server instance: check if the old one exists, kill it
    if necessary, create necessary directories and files, start
    the server. The server working directory is taken from 'vardir',
    specified in the prgoram options.
    Currently this is implemented for tarantool_silverbox only."""

    vardir = self.args.vardir

    if not silent:
      print "Installing the server..."

    if self.path_to_exe == None:
      self.path_to_exe = self.find_exe()
      self.abspath_to_exe = os.path.abspath(self.path_to_exe)

    if not silent:
      print "  Found executable at " + self.path_to_exe + "."

    if not silent:
      print "  Creating and populating working directory in " +\
      vardir + "..."

    if os.access(vardir, os.F_OK):
      if not silent:
        print "  Found old vardir, deleting..."
      self.kill_old_server()
      for filename in (glob.glob(os.path.join(vardir, "*.snap")) +
                      glob.glob(os.path.join(vardir, "*.inprogress")) +
                      glob.glob(os.path.join(vardir, "*.xlog")) +
                      glob.glob(os.path.join(vardir, "*.cfg")) +
                      glob.glob(os.path.join(vardir, "*.log")) +
                      glob.glob(os.path.join(vardir, "*.core.*")) +
                      glob.glob(os.path.join(vardir, "core"))):
        os.remove(filename)
    else:
      if (self.args.mem == True and check_tmpfs_exists() and
          os.path.basename(vardir) == vardir):
        create_tmpfs_vardir(vardir)
      else:
        os.mkdir(vardir)

    shutil.copy(self.suite_ini["config"], self.args.vardir)

    subprocess.check_call([self.abspath_to_exe, "--init_storage"],
                          cwd = self.args.vardir,
# catch stdout/stderr to not clutter output
                          stdout = subprocess.PIPE,
                          stderr = subprocess.PIPE)

    p = subprocess.Popen([self.abspath_to_exe, "--version"],
                         cwd = self.args.vardir,
                         stdout = subprocess.PIPE)
    version = p.stdout.read().rstrip()
    p.wait()

    if not silent:
      print "Starting {0} {1}.".format(os.path.basename(self.abspath_to_exe),
                                       version)

  def start(self, silent = False):

    if self.is_started:
      if not silent:
        print "The server is already started."
      return

    if not silent:
      print "Starting the server..."

    args = [self.abspath_to_exe]

    if (self.args.start_and_exit and
        not self.args.valgrind and not self.args.gdb):
      args.append("--daemonize")
    if self.args.gdb:
      args = prepare_gdb(args)
    elif self.args.valgrind:
      args = prepare_valgrind(args, self.args.valgrind_opts)

    if self.args.start_and_exit and self.args.valgrind:
      pid = os.fork()
      if pid > 0:
        os.wait()
      else:
        with daemon.DaemonContext(working_directory = self.args.vardir):
	  os.execvp(args[0], args)
    else:
      self.server = pexpect.spawn(args[0], args[1:], cwd = self.args.vardir)
      if self.args.start_and_exit:
        self.server.wait()

# wait until the server is connectedk
    if self.args.gdb and self.args.start_and_exit:
      time.sleep(1)
    elif not self.args.start_and_exit and not self.args.gdb:
      self.server.expect_exact("I am primary")
    else:
      wait_until_connected(self.suite_ini["host"], self.suite_ini["port"])

# Set is_started flag, to nicely support cleanup during an exception.
    self.is_started = True


  def stop(self, silent = False):
    """Stop server instance. Do nothing if the server is not started,
    to properly shut down the server in case of an exception during
    start up."""
    if self.is_started:
      if not silent:
        print "Stopping the server..."
      if self.args.gdb:
        self.kill_old_server(True)
      self.server.terminate()
      self.server.expect(pexpect.EOF)
      self.is_started = False
    elif not silent:
      print "The server is not started."

  def restart(self):
    self.stop(True)
    self.start(True)

  def test_option(self, option_list_str):
      args = [self.abspath_to_exe] + option_list_str.split()
      print " ".join([os.path.basename(self.abspath_to_exe)] + args[1:])
      output = subprocess.Popen(args,
                                cwd = self.args.vardir,
                                stdout = subprocess.PIPE,
                                stderr = subprocess.STDOUT).stdout.read()
      print output


  def find_exe(self):
    """Locate server executable in the bindir. We just take
    the first thing looking like an exe in there."""

    print "  Looking for server binary in {0}...".format(self.args.bindir)
    if (os.access(self.args.bindir, os.F_OK) == False or
        stat.S_ISDIR(os.stat(self.args.bindir).st_mode) == False):
      raise RuntimeError("Directory " + self.args.bindir + " doesn't exist")

    for f in os.listdir(self.args.bindir):
      f = os.path.join(self.args.bindir, f)
      st_mode = os.stat(f).st_mode
      if stat.S_ISREG(st_mode) and st_mode & stat.S_IXUSR:
        return f

    raise RuntimeError("Can't find server executable in " + self.args.bindir)

  def kill_old_server(self, silent = False):
    """Kill old server instance if it exists."""
    if os.access(self.path_to_pidfile, os.F_OK) == False:
      return # Nothing to do
    pid = 0
    with open(self.path_to_pidfile) as f:
      pid = int(f.read())
    if not silent:
      print "  Found old server, pid {0}, killing...".format(pid)
    try:
      os.kill(pid, signal.SIGTERM)
      while os.kill(pid, 0) != -1:
        time.sleep(0.001)
    except OSError:
      pass

