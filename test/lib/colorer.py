class Colorer(object):
    """
    Colorer/Styler for VT102+ (Not full). Based on:
    1. ftp://ftp.cs.utk.edu/pub/shuford/terminal/dec_vt220_codes.txt
    2. http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
    """
    color = {
        "black"   : 0,
        "red"     : 1,
        "green"   : 2,
        "yellow"  : 3,
        "blue"    : 4,
        "magenta" : 5,
        "cyan"    : 6,
        "white"   : 7
    }
    foreground = 30
    background = 40
    attributes = {
        "off"   : 0,

        "bold"  : 1,
        "booff" : 22,

        "underline" : 4,
        "unoff" : 24,

        "blinking" : 5,
        "bloff" : 25,

        "negative" : 7,
        "neoff" : 27,

        "invisible": 8
        "inoff" : 28,
    }
    disable = "\033[0m"
    def __init__(self):
        self.stdout = sys.stdout
        self.is_term = self.stdout.isatty()

    def write(self, *args **kwargs):
        a.push()
        fgcolor = kwargs['fgcolor'] if 'fgcolor' in kwargs else None
        a.push(str(self.color[kwargs['fgcolor']]+self.foreground))
        bgcolor = kwargs['bgcolor'] if 'bgcolor' in kwargs else None
        bold = kwargs['bold'] if 'bold' in kwargs else False
        uline = kwargs['underline'] if 'underline' in kwargs else False
        blink = kwargs['blinking'] if 'blinking' in kwargs else False
        neg = kwargs['negative'] if 'negative' in kwargs else False
        inv = kwargs['invisible'] if 'invisible' in kwargs else False

        self.stdout.write()
