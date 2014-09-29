import os

import sys
import shlex
import shutil
import socket

from collections import deque

from lib.admin_connection import AdminConnection

class Namespace(object):
    pass

class LuaPreprocessorException(Exception):
    def __init__(self, val):
        super(LuaPreprocessorException, self).__init__()
        self.value = val
    def __str__(self):
        return "lua preprocessor error: " + repr(self.value)

class TestState(object):
    def __init__(self, suite_ini, default_server, create_server):
        self.delimiter = ''
        self.suite_ini = suite_ini
        self.environ = Namespace()
        self.create_server = create_server
        self.servers =      { 'default': default_server }
        self.connections =  { 'default': default_server.admin }
        # curcon is an array since we may have many connections
        self.curcon = [self.connections['default']]
        nmsp = Namespace()
        setattr(nmsp, 'admin', default_server.admin.uri)
        setattr(nmsp, 'listen', default_server.sql.uri)
        setattr(self.environ, 'default', nmsp)

    def parse_preprocessor(self, string):
        token_store = deque()
        lexer = shlex.shlex(string)
        lexer.commenters = []
        token = lexer.get_token()
        if not token:
            return
        if token == 'setopt':
            option = lexer.get_token()
            if not option:
                raise LuaPreprocessorException("Wrong token for setopt: expected option name")
            value = lexer.get_token()
            if not value:
                raise LuaPreprocessorException("Wrong token for setopt: expected option value")
            return self.options(option, value)
        token_store.append(token)
        token = lexer.get_token()
        if token == 'server':
            stype = token_store.popleft()
            sname = lexer.get_token()
            if not sname:
                raise LuaPreprocessorException("Wrong token for server: expected name")
            options = {}
            temp = lexer.get_token()
            if not temp:
                pass
            elif temp == 'with':
                while True:
                    k = lexer.get_token()
                    if not k:
                        break
                    v = lexer.get_token()
                    if v == '=':
                        v = lexer.get_token()
                    options[k] = v
                    lexer.get_token()
            else:
                raise LuaPreprocessorException("Wrong token for server: expected 'with', got " + repr(temp))
            return self.server(stype, sname, options)
        elif token == 'connection':
            ctype = token_store.popleft()
            cname = [lexer.get_token()]
            if not cname[0]:
                raise LuaPreprocessorException("Wrong token for connection: expected name")
            cargs = None
            temp = lexer.get_token()
            if temp == 'to':
                cargs = lexer.get_token()
            elif temp == ',':
                while True:
                    a = lexer.get_token()
                    if not a:
                        break
                    if a == ',':
                        continue
                    cname.append(a)
            elif temp:
                raise LuaPreprocessorException("Wrong token for server: expected 'to' or ',', got " + repr(temp))
            return self.connection(ctype, cname, cargs)
        elif token == 'filter':
            ftype = token_store.popleft()
            ref = None
            ret = None
            temp = lexer.get_token()
            if temp:
                ref = temp
                if not temp:
                    raise LuaPreprocessorException("Wrong token for filter: expected filter1")
                if lexer.get_token() != 'to':
                    raise LuaPreprocessorException("Wrong token for filter: expected 'to', got {0}".format(repr(temp)))
                temp = lexer.get_token()
                if not temp:
                    raise LuaPreprocessorException("Wrong token for filter: expected filter2")
                ret = temp
            return self.filter(ftype, ref, ret)
        elif token == 'variable':
            ftype = token_store.popleft()
            ref = lexer.get_token()
            temp = lexer.get_token()
            if temp != 'to':
                raise LuaPreprocessorException("Wrong token for filter: exptected 'to', got {0}".format(repr(temp)))
            ret = lexer.get_token()
            return self.variable(ftype, ref, ret)
        else:
            raise LuaPreprocessorException("Wrong command: "+repr(lexer.instream.getvalue()))

    def options(self, key, value):
        if key == 'delimiter':
            self.delimiter = value[1:-1]
        else:
            raise LuaPreprocessorException("Wrong option: "+repr(key))

    def server(self, ctype, sname, opts):
        if ctype == 'create':
            if sname in self.servers:
                raise LuaPreprocessorException('Server {0} already exists'.format(repr(sname)))
            temp = self.create_server()
            if 'need_init' in opts:
                temp.need_init   = True if opts['need_init'] == 'True' else False
            if 'script' in opts:
                temp.script = opts['script'][1:-1]
            temp.rpl_master = None
            if 'rpl_master' in opts:
                temp.rpl_master = self.servers[opts['rpl_master']]
            temp.vardir = os.path.join(self.suite_ini['vardir'], sname)
            temp.name = sname
            self.servers[sname] = temp
            self.servers[sname].deploy(silent=True)
            nmsp = Namespace()
            setattr(nmsp, 'admin', temp.admin.port)
            setattr(nmsp, 'listen', temp.sql.port)
            if temp.rpl_master:
                setattr(nmsp, 'master', temp.rpl_master.sql.port)
            setattr(self.environ, sname, nmsp)
        elif ctype == 'start':
            if sname not in self.servers:
                raise LuaPreprocessorException('Can\'t start nonexistent server '+repr(sname))
            self.servers[sname].start(silent=True)
            self.connections[sname] = self.servers[sname].admin
            try:
                self.connections[sname]('return true', silent=True)
            except socket.error as e:
                LuaPreprocessorException('Can\'t start server '+repr(sname))
        elif ctype == 'stop':
            if sname not in self.servers:
                raise LuaPreprocessorException('Can\'t stop nonexistent server '+repr(sname))
            self.connections[sname].disconnect()
            self.connections.pop(sname)
            self.servers[sname].stop()
        elif ctype == 'deploy':
            self.servers[sname].deploy()
        elif ctype == 'cleanup':
            if sname not in self.servers:
                raise LuaPreprocessorException('Can\'t cleanup nonexistent server '+repr(sname))
            self.servers[sname].cleanup()
            if sname != 'default':
                delattr(self.environ, sname)
        else:
            raise LuaPreprocessorException('Unknown command for server: '+repr(ctype))

    def connection(self, ctype, cnames, sname):
        # we always get a list of connections as input here
        cname = cnames[0]
        if ctype == 'create':
            if sname not in self.servers:
                raise LuaPreprocessorException('Can\'t create connection to nonexistent server '+repr(sname))
            if cname in self.connections:
                raise LuaPreprocessorException('Connection {0} already exists'.format(repr(cname)))
            self.connections[cname] = AdminConnection('localhost', self.servers[sname].admin.port)
            self.connections[cname].connect()
        elif ctype == 'drop':
            if cname not in self.connections:
                raise LuaPreprocessorException('Can\'t drop nonexistent connection '+repr(cname))
            self.connections[cname].disconnect()
            self.connections.pop(cname)
        elif ctype == 'set':
            for i in cnames:
                if not i in self.connections:
                    raise LuaPreprocessorException('Can\'t set nonexistent connection '+repr(cname))
            self.curcon = [self.connections[i] for i in cnames]
        else:
            raise LuaPreprocessorException('Unknown command for connection: '+repr(ctype))

    def filter(self, ctype, ref, ret):
        if ctype == 'push':
            sys.stdout.push_filter(ref[1:-1], ret[1:-1])
        elif ctype == 'pop':
            sys.stdout.pop_filter()
        elif ctype == 'clear':
            sys.stdout.clear_all_filters()
        else:
            raise LuaPreprocessorException("Wrong command for filters: " + repr(ctype))

    def variable(self, ctype, ref, ret):
        if ctype == 'set':
            self.curcon[0](ref+'='+str(eval(ret[1:-1], {}, self.environ.__dict__)), silent=True)
        else:
            raise LuaPreprocessorException("Wrong command for variables: " + repr(ctype))

    def __call__(self, string):
        string = string[3:].strip()
        self.parse_preprocessor(string)

    def cleanup(self):
        sys.stdout.clear_all_filters()
        # don't stop the default server
        self.servers.pop('default')
        for k, v in self.servers.iteritems():
            v.stop(silent=True)
            v.cleanup()
            if k in self.connections:
                self.connections[k].disconnect()
                self.connections.pop(k)

