import shlex
from collections import deque

from lib.tarantool_server import LuaPreprocessorException

class State(object):
    def __init__(self, suite_ini):
        self.delimiter = ''
        self.suite_ini = suite_ini

    def parse(self, string):
        token_store = deque()
        lexer = shlex.shlex(string)
        lexer.commenters = []
        token = lexer.get_token()
        if not token:
            return
        if token == 'setopt':
            return OPTIONS(lexer)
        token_store.append(token)
        token = lexer.get_token()
        if token == 'server':
            stype = token_store.popleft()
            sname = lexer.get_token()
            if not sname:
                raise LuaPreprocessorException()
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
            else:
                raise LuaPreprocessorException()
            return self.server(stype, sname, options)
        elif token == 'connection':
            ctype = token_store.popleft()
            cname = lexer.get_token()
            if not cname:
                raise LuaPreprocessorException()
            cargs = None
            temp = lexer.get_token()
            if not temp:
                cargs = None
            elif temp == 'to':
                cargs = lexer.get_token()
            else:
                raise LuaPreprocessorException()
            return self.connection(ctype, cname, cargs)
        elif token == 'filter':
            ftype = token_store.popleft()
            ref = None
            ret = None
            temp = lexer.get_token()
            if temp:
                ref = temp
                if lexer.get_token() != 'to':
                    raise LuaPreprocessorException()
                temp = lexer.get_token()
                if not temp:
                    raise LuaPreprocessorException()
                ret = temp
            return self.filter(ftype, ref, ret)
        else:
            raise LuaPreprocessorException()

    def options(lexer):
        option = lexer.get_token()
        if option == 'delimiter':
            value = lexer.get_token()
            if not value:
                raise LuaPreprocessorException()
            self.delimiter = value
        else:
            raise LuaPreprocessorException()

    def server(ctype, sname, opts):
        pass
    def connection(ctype, cname, sname):
        pass
    def filter(ctype, ref, ret):
        pass

    def __call__(self, string):
        string = string[3:].strip()
        self.parse(string)
