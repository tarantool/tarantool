import os
import sys

class Singleton(type):
    _instances = {}
    def __call__(cls, *args, **kwargs):
        if cls not in cls._instances:
            cls._instances[cls] = super(Singleton, cls).__call__(*args, **kwargs)
        return cls._instances[cls]

class CSchema(object):
    objects = {}

    def __init__(self):
        self.main_objects = {
            'diff_mark': {},
            'diff_in':   {},
            'diff_out':  {},
            'test_pass': {},
            'test_fail': {},
            'test_new':  {},
            'test_skip': {},
            'test_disa': {},
            'error':     {},
            'lerror':    {},
            'tail':      {},
            'ts_text':   {},
            'path':      {},
            'info':      {},
            'separator': {},
            't_name':    {},
            'serv_text': {},
            'version':   {},
            'tr_text':   {},
        }
        self.main_objects.update(self.objects)

class SchemaAscetic(CSchema):
    objects = {
        'diff_mark': {'fgcolor': 'magenta'},
        'diff_in':   {'fgcolor': 'green'},
        'diff_out':  {'fgcolor': 'red'},
        'test_pass': {'fgcolor': 'green'},
        'test_fail': {'fgcolor': 'red'},
        'test_new':  {'fgcolor': 'lblue'},
        'test_skip': {'fgcolor': 'grey'},
        'test_disa': {'fgcolor': 'grey'},
        'error':     {'fgcolor': 'red'},
    }

class SchemaPretty(CSchema):
    objects = {
        'diff_mark': {'fgcolor': 'magenta'},
        'diff_in':   {'fgcolor': 'blue'},
        'diff_out':  {'fgcolor': 'red'},
        'test_pass': {'fgcolor': 'green'},
        'test_fail': {'fgcolor': 'red'},
        'test_new':  {'fgcolor': 'lblue'},
        'test_skip': {'fgcolor': 'grey'},
        'test_disa': {'fgcolor': 'grey'},
        'error':     {'fgcolor': 'red'},
        'lerror':    {'fgcolor': 'lred'},
        'tail':      {'fgcolor': 'lblue'},
        'ts_text':   {'fgcolor': 'lmagenta'},
        'path':      {'fgcolor': 'green',  'bold':True},
        'info':      {'fgcolor': 'yellow', 'bold':True},
        'separator': {'fgcolor': 'blue'},
        't_name':    {'fgcolor': 'lblue'},
        'serv_text': {'fgcolor': 'lmagenta'},
        'version':   {'fgcolor': 'yellow', 'bold':True},
        'tr_text':   {'fgcolor': 'green'},
    }

class Colorer(object):
    """
    Colorer/Styler based on VT220+ specifications (Not full). Based on:
    1. ftp://ftp.cs.utk.edu/pub/shuford/terminal/dec_vt220_codes.txt
    2. http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
    """
    __metaclass__ = Singleton
    fgcolor = {
        "black"    : '0;30',
        "red"      : '0;31',
        "green"    : '0;32',
        "brown"    : '0;33',
        "blue"     : '0;34',
        "magenta"  : '0;35',
        "cyan"     : '0;36',
        "grey"     : '0;37',
        "lgrey"    : '1;30',
        "lred"     : '1;31',
        "lgreen"   : '1;32',
        "yellow"   : '1;33',
        "lblue"    : '1;34',
        "lmagenta" : '1;35',
        "lcyan"    : '1;36',
        "white"    : '1;37',
    }
    bgcolor = {
        "black"    : '0;40',
        "red"      : '0;41',
        "green"    : '0;42',
        "brown"    : '0;43',
        "blue"     : '0;44',
        "magenta"  : '0;45',
        "cyan"     : '0;46',
        "grey"     : '0;47',
        "lgrey"    : '1;40',
        "lred"     : '1;41',
        "lgreen"   : '1;42',
        "yellow"   : '1;43',
        "lblue"    : '1;44',
        "lmagenta" : '1;45',
        "lcyan"    : '1;46',
        "white"    : '1;47',
    }
    attributes = {
        "bold"      : '1',
        "underline" : '4',
        "blinking"  : '5',
        "negative"  : '7',
        "invisible" : '8',
    }
    begin = "\033["
    end = "m"
    disable = begin+'0'+end

    def __init__(self):
        self.stdout = sys.stdout
        self.is_term = self.stdout.isatty()
        self.colors = int(os.popen('tput colors').read()) if self.is_term else None
        print os.getenv('TT_SCHEMA')
        schema = os.getenv('TT_SCHEMA', 'ascetic')
        if schema == 'ascetic':
            self.schema = SchemaAscetic()
        elif schema == 'pretty':
            self.schema = SchemaPretty()
        else:
            self.schema = CSchema()
        self.schema = self.schema.main_objects

    def set_stdout(self):
        sys.stdout = self

    def ret_stdout(self):
        sys.stdout = self.stdout

    def write(self, *args, **kwargs):
        flags = []
        if 'schema' in kwargs:
            kwargs.update(self.schema[kwargs['schema']])
        for i in self.attributes:
            if i in kwargs and kwargs[i] == True:
                flags.append(self.attributes[i])
        flags.append(self.fgcolor[kwargs['fgcolor']]) if 'fgcolor' in kwargs else None
        flags.append(self.bgcolor[kwargs['bgcolor']]) if 'bgcolor' in kwargs else None

        if self.is_term:
            self.stdout.write(self.begin+';'.join(flags)+self.end)
        for i in args:
            self.stdout.write(str(i))
        if self.is_term:
            self.stdout.write(self.disable)
        self.stdout.flush()

    def __call__(self, *args, **kwargs):
        self.write(*args, **kwargs)

    def writeout_unidiff(self, diff):
        for i in diff:
            if i.startswith('+'):
                self.write(i, schema='diff_in')
            elif i.startswith('-'):
                self.write(i, schema='diff_out')
            elif i.startswith('@'):
                self.write(i, schema='diff_mark')

    def flush(self):
        return self.stdout.flush()

    def fileno(self):
        return self.stdout.fileno()

    def isatty(self):
        return self.is_term
