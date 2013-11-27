import os
import sys
import shlex
import socket

from collections import deque

from lib.admin_connection import AdminConnection

class LuaPreprocessorException(Exception):
    def __init__(self, val):
        super(LuaPreprocessorException, self).__init__()
        self.value = val
    def __str__(self):
        return "lua preprocessor error: " + repr(self.value)

class State(object):
    def __init__(self, suite_ini, curcon, server):
        self.delimiter = ''
        self.suite_ini = suite_ini
        self.curcon = [curcon]
        self.tarantool_server = server

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
                raise LuaPreprocessorException() #TODO
            value = lexer.get_token()
            if not value:
                raise LuaPreprocessorException() #TODO
            return self.options(option, value)
        token_store.append(token)
        token = lexer.get_token()
        if token == 'server':
            stype = token_store.popleft()
            sname = lexer.get_token()
            if not sname:
                raise LuaPreprocessorException() #TODO
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
                raise LuaPreprocessorException() #TODO
            return self.server(stype, sname, options)
        elif token == 'connection':
            ctype = token_store.popleft()
            cname = [lexer.get_token()]
            if not cname[0]:
                raise LuaPreprocessorException() #TODO
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
                raise LuaPreprocessorException() #TODO
            return self.connection(ctype, cname, cargs)
        elif token == 'filter':
            ftype = token_store.popleft()
            ref = None
            ret = None
            temp = lexer.get_token()
            if temp:
                ref = temp
                if lexer.get_token() != 'to':
                    raise LuaPreprocessorException() #TODO
                temp = lexer.get_token()
                if not temp:
                    raise LuaPreprocessorException() #TODO
                ret = temp
            return self.filter(ftype, ref, ret)
        else:
            raise LuaPreprocessorException() #TODO

    def options(self, key, value):
        if key == 'delimiter':
            self.delimiter = value[1:-1]
        else:
            raise LuaPreprocessorException() #TODO

    def server(self, ctype, sname, opts):
        if ctype == 'create':
            temp = self.tarantool_server()
            if 'configuration' in opts:
                temp.config = opts['configuration'][1:-1]
            else:
                temp.cofnfig = self.suite_ini['config']
            if 'need_init' in opts:
                temp.need_init = True if opts['need_init'] == 'True' else False
            if 'init' in opts:
                temp.init_lua = params['init'][1:-1]
            temp.vardir = os.path.join(self.suite_ini['vardir'], sname)
            temp.binary = temp.find_exe(self.suite_ini['builddir'])
            self.suite_ini['servers'][sname] = temp
            temp.configure(temp.config)
            temp.install(temp.binary,
                    temp.vardir, temp.mem, True)
            if temp.need_init:
                temp.init()
        elif ctype == 'start':
            if not (sname in self.suite_ini['servers']):
                raise LuaPreprocessprException() #TODO
            self.suite_ini['servers'][sname].start(silent=True)
            self.suite_ini['connections'][sname] = [self.suite_ini['servers'][sname].admin, sname]
            try:
                self.suite_ini['connections'][sname][0]('print()', silent=True)
            except socket.error as e:
                LuaPreprocessorException() #TODO
        elif ctype == 'stop':
            self.suite_ini['servers'][sname].stop()
            for cname in [k for k, v in self.suite_ini['connections'].iteritems() if v[1] == 'sname']:
                self.suite_ini['connections'][cname].disconnect()
                self.suite_ini['connections'].pop(cname)
        elif ctype == 'deploy':
            pass
        elif ctype == 'reconfigure':
            if not (sname in self.suite_ini['servers']):
                raise LuaPreprocessorException() #TODO
            temp = self.suite_ini['servers'][sname]
            if 'configuration' in opts:
                temp.reconfigure(opts['configuration'][1:-1], silent = True)
            else:
                temp.cofnfig = self.suite_ini['config']
                if temp.init_lua != None:
                    var_init_lua = os.path.join(temp.vardir, temp.default_init_lua_name)
                    if os.path.exists(var_init_lua):
                        os.path.remove(var_init_lua)
                if 'init' in params:
                    temp.init_lua = params['init'][1:-1]
                    var_init_lua = os.path.join(temp.vardir, temp.default_init_lua_name)
                    shutil.copy(temp.init_lua, var_init_lua)
                    temp.restart()
        elif ctype == 'cleanup':
            if sname not in self.suite_ini['servers']:
                raise LuaPreprocessorException() #TODO
            self.suite_ini['servers'][sname].cleanup()
        else:
            raise LuaPreprocessorException() #TODO

    def connection(self, ctype, cname, sname):
        if ctype == 'create':
            if sname not in self.suite_ini['servers']:
                raise LuaPreprocessorException() #TODO
            if cname[0] in self.suite_ini['connections']:
                raise LuaPreprocessorException() #TODO
            self.suite_ini['connections'][cname[0]] = [AdminConnection('localhost',
                    self.suite_ini['servers'][sname].port), sname]
            self.suite_ini['connections'][cname[0]][0].connect()
        elif ctype == 'drop':
            if cname[0] not in self.suite_ini['connections']:
                raise LuaPreprocessorException() #TODO
            self.suite_ini['connections'][cname[0]][0].disconnect()
            self.suite_ini['connections'].pop(cname[0])
        elif ctype == 'set':
            for i in cname:
                if not i in self.suite_ini['connections']:
                    raise LuaPreprocessorException() #TODO
            self.curcon = [self.suite_ini['connections'][i][0] for i in cname]
        else:
            raise LuaPreprocessorException() #TOD#O

    def filter(self, ctype, ref, ret):
        if ctype == 'push':
            sys.stdout.push_filter(ref[1:], ret[:-1])
        elif ctype == 'pop':
            sys.stdout.pop_filter()
        elif ctype == 'clear':
            sys.stdout.clear_all_filters()
        else:
            raise LuaPreprocessorException("Wrong command for filters: " + repr(ctype))

    def __call__(self, string):
        string = string[3:].strip()
        self.parse_preprocessor(string)

    def flush(self):
        sys.stdout.clear_all_filters()
        a = self.suite_ini['servers']['default']
        self.suite_ini['servers'].pop('default')
        for k, v in self.suite_ini['servers'].iteritems():
            v.stop(silent=True)
            v.cleanup()
            for cname in [name for name, tup in self.suite_ini['connections'].iteritems() if tup[1] == 'sname']:
                self.suite_ini['connections'][cname].disconnect()
                self.suite_ini['connections'].pop(cname)
        self.suite_ini['servers']['default'] = a

