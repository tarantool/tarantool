import os
import re
import sys
import glob
import time
import yaml
import errno
import shlex
import daemon
import random
import shutil
import signal
import socket
import difflib
import filecmp
import traceback
import subprocess
import collections


try:
    from cStringIO import StringIO
except ImportError:
    from StringIO import StringIO

from lib.test import Test
from lib.server import Server
from lib.preprocessor import State
from lib.box_connection import BoxConnection
from lib.admin_connection import AdminConnection

from lib.colorer import Colorer
color_stdout = Colorer()

def check_port(port, rais=True):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect(("localhost", port))
    except socket.error:
        return True
    if rais:
        raise RuntimeError("The server is already running on port {0}".format(port))
    return False

def find_port(port):
    while port < 65536:
        if check_port(port, False):
            return port
        port += 1
    return find_port(34000)

def find_in_path(name):
    path = os.curdir + os.pathsep + os.environ["PATH"]
    for _dir in path.split(os.pathsep):
        exe = os.path.join(_dir, name)
        if os.access(exe, os.X_OK):
            return exe
    return ''


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
                cmd = StringIO()
            if line.find('--#') == 0:
                rescom = cmd.getvalue().replace('\n\n', '\n')
                if rescom:
                    sys.stdout.write(cmd.getvalue())
                    result = send_command(rescom)
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
                    sys.stdout.write(cmd.getvalue())
                    rescom = cmd.getvalue()[:delim_len].replace('\n\n', '\n')
                    result = send_command(rescom)
                    sys.stdout.write(result.replace("\r\n", "\n"))
                    cmd.close()
                    cmd = None
        ts.flush()


class PythonTest(FuncTest):
    def execute(self, server):
        execfile(self.name, dict(locals(), **server.__dict__))


class TarantoolConfig(object):
    def __init__(self, path):
        self.path = path

    def parse(self):
        cfg = {}
        with open(self.path, 'r') as f:
            for line in f:
                line = [part.strip() for part in line.split('=', 1)]
                if not line or not line[0] or line[0][0] == '#':
                    continue
                if len(line) != 2:
                    raise Exception("Bad cfg line: {line}. file: {file}".format(\
                                    line = repr(line), file = repr(self.path)))
                cfg[line[0]] = line[1]
        return cfg

    def generate(self, original):
        with open(self.path, 'w') as f:
            for el in original.iteritems():
                f.write(' = '.join(el) + '\n')

class TarantoolLog(object):
    def __init__(self, path):
        self.path = path
        self.log_begin = 0

    def positioning(self):
        if os.path.exists(self.path):
            with open(self.path, 'r') as f:
                f.seek(0, os.SEEK_END)
                self.log_begin = f.tell()

    def seek_from(self, msg, proc=None):
        while True:
            if os.path.exists(self.path):
                break
            time.sleep(0.001)

        with open(self.path, 'r') as f:
            f.seek(self.log_begin, os.SEEK_SET)
            cur_pos = self.log_begin
            while True:
                if not (proc is None):
                    if not (proc.poll() is None):
                        raise OSError("Can't start Tarantool")
                log_str = f.readline()
                if not log_str:
                    time.sleep(0.001)
                    f.seek(cur_pos, os.SEEK_SET)
                    continue
                if log_str.find(msg) != -1:
                    return
                cur_pos = f.tell()

class Mixin(object):
    pass

class ValgrindMixin(Mixin):
    default_valgr = {
            "logfile":        "valgrind.log",
            "suppress_path":        "share/",
            "suppress_name": "tarantool.sup"}

    @property
    def valgrind_log(self):
        return os.path.join(self.vardir, self.default_valgr['logfile'])

    @property
    def valgrind_sup(self):
        if not hasattr(self, '_valgrind_sup') or not self._valgrind_sup:
            return os.path.join(self.testdir,
                                self.default_valgr['suppress_path'],
                                self.default_valgr['suppress_name'])
        return self._valgrind_sup
    @valgrind_sup.setter
    def valgrind_sup(self, val):
        self._valgrind_sup = os.path.abspath(val)

    @property
    def valgrind_sup_output(self):
        return os.path.join(self.vardir, self.default_valgr['suppress_name'])

    def prepare_args(self):
        if not find_in_path('valgrind'):
            raise OSError('`valgrind` executables not found in PATH')
        return  shlex.split("valgrind --log-file={log} --suppressions={sup} \
                --gen-suppressions=all --leak-check=full \
                --read-var-info=yes --quiet {bin}".format(log = self.valgrind_log,
                                                        sup = self.valgrind_sup,
                                                        bin = self.init_lua if self.shebang else self.binary))

    def wait_stop(self):
        return self.process.wait()

class GdbMixin(Mixin):
    default_gdb = {
        "name": "tarantool-gdb"}

    def start_and_exit(self):
        color_stdout('You started the server in gdb mode.\n', schema='info')
        color_stdout('To attach, use `screen -r tarantool-gdb`\n', schema='info')
        TarantoolServer.start_and_exit(self)


    def prepare_args(self):
        if not find_in_path('screen'):
            raise OSError('`screen` executables not found in PATH')
        if not find_in_path('gdb'):
            raise OSError('`gdb` executables not found in PATH')
        color_stdout('You started the server in gdb mode.\n', schema='info')
        color_stdout('To attach, use `screen -r tarantool-gdb`\n', schema='info')
        return shlex.split("screen -dmS {0} gdb {1} -ex \
                \'b main\' -ex \'run >> {2} 2>> {2}\'".format(self.default_gdb['name'],
                                                       self.init_lua if self.shebang else self.binary,
                                                       self.logfile))

    def wait_stop(self):
        self.kill_old_server()
        self.process.wait()

class TarantoolServer(Server):
    default_tarantool = {
            "bin":       "tarantool_box",
            "config":    "tarantool.cfg",
            "logfile":   "tarantool.log",
            "init":           "init.lua",
            "pidfile":         "box.pid",
            "name":            "default"}
    generate_ports = [
            'primary_port',
            'admin_port',
            ]
    generated_props = [
            'replication_source'
            ]
#----------------------------------PROPERTIES----------------------------------#
    @property
    def debug(self):
        return self.test_debug()
    @property
    def name(self):
        if not hasattr(self, '_name') or not self._name:
            return self.default_tarantool["name"]
        return self._name
    @name.setter
    def name(self, val):
        self._name = val

    @property
    def logfile(self):
        if not hasattr(self, '_logfile') or not self._logfile:
            return os.path.join(self.vardir, self.default_tarantool["logfile"])
        return self._logfile
    @logfile.setter
    def logfile(self, val):
        self._logfile = os.path.join(self.vardir, val)

    @property
    def pidfile(self):
        if not hasattr(self, '_pidfile') or not self._pidfile:
            return os.path.join(self.vardir, self.default_tarantool["pidfile"])
        return self._pidfile
    @pidfile.setter
    def pidfile(self, val):
        self._pidfile = os.path.join(self.vardir, val)

    @property
    def cfgfile(self):
        if not hasattr(self, '_cfgfile') or not self._cfgfile:
            return os.path.join(self.vardir, self.default_tarantool["config"])
        return self._cfgfile
    @cfgfile.setter
    def cfgfile(self, val):
        self._cfgfile = os.path.join(self.vardir, val)

    @property
    def cfgfile_source(self):
        if not hasattr(self, '_cfgfile_source'):
            raise ValueError("No config-file is specified")
        return self._cfgfile_source
    @cfgfile_source.setter
    def cfgfile_source(self, path):
        if path == None:
            if hasattr(self, '_cfgfile_source'):
                delattr(self, '_cfgfile_source')
            return
        self._cfgfile_source = os.path.abspath(path)

    @property
    def init_lua_source(self):
        if not hasattr(self, '_init_lua_source'): self._init_lua_source = None
        return self._init_lua_source
    @init_lua_source.setter
    def init_lua_source(self, val):
        if val is None:
            return
        self._init_lua_source = os.path.abspath(val)

    @property
    def builddir(self):
        if not hasattr(self, '_builddir'):
            raise ValueError("No build-dir is specified")
        return self._builddir
    @builddir.setter
    def builddir(self, val):
        if val is None:
            return
        self._builddir = os.path.abspath(val)

    @property
    def init_lua(self):
        return os.path.join(self.vardir, self.default_tarantool['init'])

    @property
    def logfile_pos(self):
        if not hasattr(self, '_logfile_pos'): self._logfile_pos = None
        return self._logfile_pos
    @logfile_pos.setter
    def logfile_pos(self, val):
        self._logfile_pos = TarantoolLog(val)
        self._logfile_pos.positioning()

    @property
    def shebang(self):
        if not hasattr(self, '_shebang'): self._shebang = None
        return self._shebang
    @shebang.setter
    def shebang(self, val):
        if val is None:
            if hasattr(self, '_shebang'):
                delattr(self, '_shebang')
            return
        self._shebang = os.path.abspath(val)

    @property
    def _admin(self):
        if not hasattr(self, 'admin'): self.admin = None
        return self.admin
    @_admin.setter
    def _admin(self, port):
        try:
            int(port)
        except ValueError as e:
            raise ValueError("Bad port number: '%s'" % port)
        if not hasattr(self, 'admin') or self.admin is None:
            self.admin = AdminConnection('localhost', port)
            return
        if self.admin.port != port:
            self.admin.port = port
            self.admin.reconnect()

    @property
    def _sql(self):
        if not hasattr(self, 'sql'): self.sql = None
        return self.sql
    @_sql.setter
    def _sql(self, port):
        try:
            port = int(port)
        except ValueError as e:
            raise ValueError("Bad port number: '%s'" % port)
        if not hasattr(self, 'sql') or self.sql is None:
            self.sql = BoxConnection('localhost', port)
            return
        if self.sql.port != port:
            self.sql.port = port
            self.sql.reconnect()

    @property
    def log_des(self):
        if not hasattr(self, '_log_des'): self._log_des = open(self.logfile, 'a')
        return self._log_des
    @log_des.deleter
    def log_des(self):
        if not hasattr(self, '_log_des'): return
        if not self._log_des.closed: self._log_des.closed()
        delattr(self, _log_des)

    @property
    def rpl_master(self):
        if not hasattr(self, '_rpl_master'): self._rpl_master = None
        return self._rpl_master
    @rpl_master.setter
    def rpl_master(self, val):
        if not isinstance(self, (TarantoolServer, None)):
            raise ValueError('Replication master must be Tarantool'
                    ' Server class, his derivation or None')
        self._rpl_master = val

    @property
    def hot_master(self):
        if not hasattr(self, '_hot_master'): self._hot_master = None
        return self._hot_master
    @hot_master.setter
    def hot_master(self, val):
        if not isinstance(self, (TarantoolServer, None)):
            raise ValueError('Hot-standby master must be Tarantool'
                    ' Server class, his derivation or None')
        self._hot_master = val


#------------------------------------------------------------------------------#

    def __new__(cls, ini=None):
        if ini is None:
            ini = {'core': 'tarantool'}
        if ('valgrind' in ini and ini['valgrind']) and ('gdb' in ini and ini['gdb']):
            raise OSError('Can\'t run under valgrind and gdb simultaniously')
        if 'valgrind' in ini and ini['valgrind']:
            cls = type('ValgrindTarantooServer', (ValgrindMixin, TarantoolServer), {})
        elif 'gdb' in ini and ini['gdb']:
            cls = type('GdbTarantoolServer', (GdbMixin, TarantoolServer), {})

        return super(TarantoolServer, cls).__new__(cls)

    def __init__(self, _ini=None):
        if _ini is None:
            _ini = {}
        ini = {
            'config': None,
            'core': 'tarantool',
            'gdb': False,
            'init_lua': None,
            'lua_libs': [],
            'random_ports': True,
            'valgrind': False,
            'vardir': None,
            'start_and_exit': False
        }; ini.update(_ini)
        Server.__init__(self, ini)
        self.generated_fields = self.generate_ports + self.generated_props
        self.testdir = os.path.abspath(os.curdir)
        self.re_vardir_cleanup += [
            "*.snap", "*.xlog", "*.inprogress",
            "*.cfg", "*.sup", "*.lua", "*.pid"]
        self.name = "default"
        self.conf = {}
        self.status = None
        #-----InitBasicVars-----#
        self.cfgfile_source = ini['config']
        self.core = ini['core']
        self.gdb = ini['gdb']
        self.init_lua_source = ini['init_lua']
        self.lua_libs = ini['lua_libs']
        self.random_ports = ini['random_ports']
        self.valgrind = ini['valgrind']
        self._start_and_exit = ini['start_and_exit']

    def __del__(self):
        self.stop()

    @classmethod
    def find_exe(cls, builddir, silent=True):
        cls.builddir = os.path.abspath(builddir)
        builddir = os.path.join(builddir, "src/box")
        path = builddir + os.pathsep + os.environ["PATH"]
        if not silent:
            color_stdout("Looking for server binary in ", schema='serv_text')
            color_stdout(path + ' ...\n', schema='path')
        for _dir in path.split(os.pathsep):
            exe = os.path.join(_dir, cls.default_tarantool["bin"])
            if os.access(exe, os.X_OK):
                cls.binary = os.path.abspath(exe)
                return exe
        raise RuntimeError("Can't find server executable in " + path)

    def install(self, silent=True):
        if not silent:
            color_stdout('Installing the server ...\n', schema='serv_text')
            color_stdout('    Found executable at ', schema='serv_text')
            color_stdout(self.binary + '\n', schema='path')
            color_stdout('    Creating and populating working directory in ', schema='serv_text')
            color_stdout(self.vardir + ' ...\n', schema='path')
        if not os.path.exists(self.vardir):
            os.makedirs(self.vardir)
        else:
            if not silent:
                color_stdout('    Found old vardir, deleting ...\n', schema='serv_text')
            self.kill_old_server()
            self.cleanup()
        self.copy_files()
        self.configure()
        return

    def deploy(self, silent=True):
        self.install(silent)
        if not self._start_and_exit:
            self.start(silent)
        else:
            self.start_and_exit()

    def configure(self, config=None):
        self.copy_config(config)
        self.port   = self.conf['admin_port']
        self._sql    = self.conf['primary_port']
        self._admin  = self.conf['admin_port']

    def reconfigure(self, config, silent=False, override=['all']):
        if config == None:
            os.unlink(self.cfgfile)
        else:
            self.cfgfile_source = config
            self.copy_config(override=override)
        self.admin.execute("box.cfg.reload()", silent=silent)

    def copy_config(self, rand=True, override = ['all']):
        override_all = (True if 'all' in override else False)

        port = random.randrange(3300, 9999)
        for t in self.generate_ports:
            if not t in self.conf:
                self.conf[t] = find_port(port)
                port += 1
        if not self.hot_master is None:
            self.conf['primary_port'] = self.hot_master.sql.port
        if not self.rpl_master is None and 'replication_source' in self.generated_fields:
            self.conf['replication_source'] = \
                '127.0.0.1:'+str(self.rpl_master.conf['primary_port'])


        basic = TarantoolConfig(self.cfgfile_source).parse()
        addit = {}
        for key in self.generated_fields:
            if key in basic and (override_all or key in override) and key in self.conf:
                addit[key] = str(self.conf[key])
        basic.update(addit)
        TarantoolConfig(self.cfgfile).generate(basic)

    def copy_files(self):
        if self.shebang:
            shutil.copy(self.shebang, self.init_lua)
            os.chmod(self.init_lua, 0777)
        elif self.init_lua_source:
            shutil.copy(self.init_lua_source, self.init_lua)
        if self.lua_libs:
            for i in self.lua_libs:
                source = os.path.join(self.testdir, i)
                shutil.copy(source, self.vardir)

    def prepare_args(self):
        return shlex.split(self.init_lua if self.shebang else self.binary)

    def start_and_exit(self):
        color_stdout('Starting the server {0} on ports {1} ...\n'.format(
            os.path.basename(self.binary) if not self.shebang else self.shebang,
            ', '.join([': '.join([str(j) for j in i]) for i in self.conf.items() if i[0].find('port') != -1])
            ), schema='serv_text')
        with daemon.DaemonContext():
            self.start()
            self.process.wait()

    def start(self, silent=True):
        if self.status == 'started':
            if not silent:
                color_stdout('The server is already started.\n', schema='lerror')
            return
        if not silent or self._start_and_exit:
            color_stdout("Starting the server ...\n", schema='serv_text')
            color_stdout("Starting ", schema='serv_text')
            color_stdout((os.path.basename(self.binary) if not self.shebang else self.shebang) + " \n", schema='path')
            color_stdout(self.version() + "\n", schema='version')

        check_port(self.conf['admin_port'])

        args = self.prepare_args()
        self.logfile_pos = self.logfile
        self.process = subprocess.Popen(args,
                cwd = self.vardir,
                stdout=self.log_des,
                stderr=self.log_des)
        self.wait_until_started()
        self.status = 'started'

    def wait_stop(self):
        self.process.wait()

    def stop(self, silent=True):
        if self.status != 'started':
            if not silent:
                color_stdout('The server is not started.\n', schema='lerror')
            return
        if not silent:
            color_stdout('Stopping the server ...\n', schema='serv_text')
        self.process.terminate()
        self.wait_stop()
        self.status = None

    def restart(self):
        self.stop()
        self.start()

    def kill_old_server(self, silent=True):
        pid = self.read_pidfile()
        if pid == -1:
            return False
        if not silent:
            color_stdout('    Found old server, pid {0}, killing ...'.format(pid), schema='info')
        try:
            os.kill(pid, signal.SIGTERM)
        except OSError:
            pass
        self.wait_until_stopped(pid)
        return True

    def wait_until_started(self):
        """ Wait until server is started.

        Server consists of two parts:
        1) wait until server is listening on sockets
        2) wait until server tells us his status

        """

        self.logfile_pos.seek_from('entering the event loop\n', self.process if not self.gdb else None)
        while True:
            try:
                temp = AdminConnection('localhost', self.conf['admin_port'])
                ans = yaml.load(temp.execute('box.info.status'))[0]
                if ans in ('primary', 'hot_standby', 'orphan') or ans.startswith('replica'):
                    return True
                else:
                    raise Exception("Strange output for `box.info.status`: %s" % (ans))
            except socket.error as e:
                if e.errno == errno.ECONNREFUSED:
                    time.sleep(0.1)
                    continue
                raise

    def wait_until_stopped(self, pid):
        while True:
            try:
                time.sleep(0.01)
                os.kill(pid, 0)
                continue
            except OSError as err:
                break

    def read_pidfile(self):
        pid = -1
        if os.path.exists(self.pidfile):
            try:
                with open(self.pidfile) as f:
                    pid = int(f.read())
            except:
                pass
        return pid

    def print_log(self, lines):
        color_stdout("\nLast {0} lines of Tarantool Log file:\n".format(lines), schema='error')
        with open(self.logfile, 'r') as log:
            return log.readlines()[-lines:]

    def test_option_get(self, option_list_str, silent=False):
        args = [self.binary] + shlex.split(option_list_str)
        if not silent:
            print " ".join([os.path.basename(self.binary)] + args[1:])
        output = subprocess.Popen(args, cwd = self.vardir, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT).stdout.read()
        return output

    def test_option(self, option_list_str):
        print self.test_option_get(option_list_str)

    def test_debug(self):
        if self.test_option_get("-V", True).find("-Debug"):
            return True
        return False

    def find_tests(self, test_suite, suite_path):
        def patterned(test, patterns):
            return [test for i in patterns if test.name.find(i) != -1]

        tests  = [PythonTest(k, test_suite.args, test_suite.ini) \
                for k in sorted(glob.glob(os.path.join(suite_path, "*.test.py" )))]
        tests += [LuaTest(k, test_suite.args, test_suite.ini)    \
                for k in sorted(glob.glob(os.path.join(suite_path, "*.test.lua")))]
        test_suite.tests = sum(map((lambda x: patterned(x, test_suite.args.tests)), tests), [])

    def get_param(self, param = None):
        if not param is None:
            return yaml.load(self.admin("box.info." + param, silent=True))[0]
        return yaml.load(self.admin("box.info", silent=True))

    def wait_lsn(self, lsn):
        while (int(self.get_param("lsn")) < lsn):
            time.sleep(0.01)

    def version(self):
        p = subprocess.Popen([self.binary, "--version"],
                             cwd = self.vardir,
                             stdout = subprocess.PIPE)
        version = p.stdout.read().rstrip()
        p.wait()
        return version
