import os
import stat 
import shutil
import subprocess
import pexpect
import sys
import signal
import time

class TarantoolSilverboxServer:
  """Server represents a single server instance. Normally, the
  program operates with only one server, but in future we may add
  replication slaves. The server is started once at the beginning
  of each suite, and stopped at the end."""

  def __init__(self, args, config, pidfile):
    """Set server options: path to configuration file, pid file, exe, etc."""
    self.args = args
    self.path_to_config = config
    self.path_to_pidfile = os.path.join(args.vardir, pidfile)
    self.path_to_exe = None
    self.abspath_to_exe = None
    self.is_started = False

  def start(self):
    """Start server instance: check if the old one exists, kill it
    if necessary, create necessary directories and files, start
    the server. The server working directory is taken from 'vardir',
    specified in the prgoram options.
    Currently this is implemented for tarantool_silverbox only."""

    if not self.is_started:
      print "Starting the server..."

      if self.path_to_exe == None:
        self.path_to_exe = self.find_exe() 
        self.abspath_to_exe = os.path.abspath(self.path_to_exe)

      print "  Found executable at " + self.path_to_exe + "."

      print "  Creating and populating working directory in " +\
      self.args.vardir + "..." 

      if os.access(self.args.vardir, os.F_OK):
        print "  Found old vardir, deleting..."
        self.kill_old_server()
        shutil.rmtree(self.args.vardir, ignore_errors = True)

      os.mkdir(self.args.vardir)
      shutil.copy(self.path_to_config, self.args.vardir)

      subprocess.check_call([self.abspath_to_exe, "--init_storage"],
                            cwd = self.args.vardir,
# catch stdout/stderr to not clutter output
                            stdout = subprocess.PIPE,
                            stderr = subprocess.PIPE)

      if self.args.start_and_exit:
        subprocess.check_call([self.abspath_to_exe, "--daemonize"],
                              cwd = self.args.vardir,
                              stdout = subprocess.PIPE,
                              stderr = subprocess.PIPE)
      else:
        self.server = pexpect.spawn(self.abspath_to_exe,
                                    cwd = self.args.vardir)
        self.logfile_read = sys.stdout
        self.server.expect_exact("entering event loop")

      version = subprocess.Popen([self.abspath_to_exe, "--version"],
                                 cwd = self.args.vardir,
                                 stdout = subprocess.PIPE).stdout.read().rstrip()

      print "Started {0} {1}.".format(os.path.basename(self.abspath_to_exe),
                                      version)

# Set is_started flag, to nicely support cleanup during an exception.
      self.is_started = True
    else:
      print "The server is already started."

  def stop(self):
    """Stop server instance. Do nothing if the server is not started,
    to properly shut down the server in case of an exception during
    start up."""
    if self.is_started:
      print "Stopping the server..."
      self.server.terminate()
      self.server.expect(pexpect.EOF)
      self.is_started = False
    else:
      print "The server is not started."

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
      raise RuntimeError("Can't find server executable in " +
                         self.args.bindir)

  def kill_old_server(self):
    """Kill old server instance if it exists."""
    if os.access(self.path_to_pidfile, os.F_OK) == False:
      return # Nothing to do
    pid = 0
    with open(self.path_to_pidfile) as f:
      pid = int(f.read()) 
    print "  Found old server, pid {0}, killing...".format(pid)
    try:
      os.kill(pid, signal.SIGTERM)
      while os.kill(pid, 0) != -1:
        time.sleep(0.01)
    except OSError:
      pass

