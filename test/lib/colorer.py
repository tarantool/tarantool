import os
import sys

class Singleton(type):
    _instances = {}
    def __call__(cls, *args, **kwargs):
        if cls not in cls._instances:
            cls._instances[cls] = super(Singleton, cls).__call__(*args, **kwargs)
        return cls._instances[cls]

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

    def set_stdout(self):
        sys.stdout = self

    def ret_stdout(self):
        sys.stdout = self.stdout

    def write(self, *args, **kwargs):
        flags = []
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
                self.write(i, fgcolor='blue')
            elif i.startswith('-'):
                self.write(i, fgcolor='red')
            elif i.startswith('@'):
                self.write(i, fgcolor='magenta')

    def flush(self):
        return self.stdout.flush()

    def fileno(self):
        return self.stdout.fileno()

    def isatty(self):
        return self.is_term
