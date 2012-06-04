import os
import shutil
import subprocess
import pexpect
import ConfigParser
from server import Server

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
    def __new__(cls, core=None, module=None):
        if module  == None:
            return super(Server, cls).__new__(cls)
        mdlname = "lib.{0}_{1}_server".format(core, module)
        clsname = "{0}{1}Server".format(core.title(), module.title())
        modulecls = __import__(mdlname, fromlist=clsname).__dict__[clsname]

        return modulecls.__new__(modulecls, core, module)

    def __init__(self, core, module):
        Server.__init__(self, core, module)
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
        return Server.find_exe(self, "{0}/mod/{1}".format(builddir, self.module), silent)

    def configure(self, config):
        Server.configure(self, config)
        # now read the server config, we need some properties from it
        with open(self.config) as fp:
            dummy_section_name = "tarantool"
            config = ConfigParser.ConfigParser()
            config.readfp(TarantoolConfigFile(fp, dummy_section_name))
            self.pidfile = config.get(dummy_section_name, "pid_file")

    def reconfigure(self, config, silent=False):
        if config == None:
            os.unlink(os.path.join(self.vardir, self.default_config_name))
        else:
            self.config = os.path.abspath(config)
            shutil.copy(self.config, os.path.join(self.vardir, self.default_config_name))
        self.admin.execute("reload configuration", silent=silent)

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
        return "{0}_{1}".format(self.core, self.module)

