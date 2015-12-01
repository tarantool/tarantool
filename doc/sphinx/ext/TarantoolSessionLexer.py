# -*- coding: utf-8 -*-

from pygments.lexer import Lexer, RegexLexer, include, bygroups, using, \
    default, words, combined, do_insertions
from pygments.util import get_bool_opt, shebang_matches
from pygments.token import Text, Comment, Operator, Keyword, Name, String, \
    Number, Punctuation, Generic, Other, Error
from pygments import unistring as uni

from LuaLexer import LuaLexer
from pygments.lexers import YamlLexer, BashSessionLexer

import re

__all__ = ['TarantoolSessionLexer']

line_re = re.compile('.*?\n')

yml_beg = u"---"
yml_end = u"..."

uriverify = re.compile(
#    r'^(?:http|ftp)s?://' # http:// or https://
    r'^'
    r'(?:(?:[A-Z0-9](?:[A-Z0-9-]{0,61}[A-Z0-9])?\.)+(?:[A-Z]{2,6}\.?|[A-Z0-9-]{2,}\.?)|' #domain...
    r'localhost|' # localhost...
    r'tarantool|' # OR tarantool, we're tarantool, of course
    r'\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})' # ...or ip
    r'(?::\d+)?' # optional port
    r'(?:/?|[/?]\S+)$', re.IGNORECASE
)

def find_prompt(line):
    pos = line.find('> ')
    if pos != -1 and uriverify.match(line[:pos]):
        return pos
    return False

class TarantoolSessionLexer(Lexer):
    """
    For Tarantol session output:

    .. sourcecode:: tarantoolsession

        $ <bash session - preparations>
        tarantool> ...
                 > ...
        ---
        - yml_output
        ...
    """

    name = "Tarantool console session"
    aliases = ["tarantoolsession", "tntses"]
    mimetypes = ['text/x-tarantool-session']

    def __init__(self, **options):
        super(TarantoolSessionLexer, self).__init__()

    def get_tokens_unprocessed(self, text):
        lualexer = LuaLexer(**self.options)
        ymllexer = YamlLexer(**self.options)
        shslexer = BashSessionLexer(**self.options)

        curcode = ''
        insertions = []
        code = False
        prompt_len = 0

        curyml = ''
        yml = False
        ymlcnt = 0

        curshs = ''
        shs = False

        for match in line_re.finditer(text):
            line = match.group()
            # print line
            # print code, yml, shs

            # First part - if output starts from '$ ' then it's BASH session
            # - We must only check that we're not inside of YAML
            # code can't start with '$ '
            # if output (not inside YAML) starts with '$ ' - it's not our problem
            # Also, we can match multiline commands only if line ends with '\'
            check_shs = (line.startswith('$ ') and not yml) or shs
            if check_shs:
                curshs += line
                if line.endswith('\\'):
                    shs = True
                    continue
                for item in shslexer.get_tokens_unprocessed(curshs):
                    yield item
                curshs = ''
                shs = False
                continue

            # Second part - check for YAML
            # 1) It's begin, means (yml == False) and line.strip() == '---'
            # 2) It's middle. (yml == True) and line.strip() not in ('---', '...')
            # 3) It's end - then (yml == False) and line.strip() == '...']
            check_yml_begin  = (yml == False and line.strip()     in (yml_beg, ))
            # check_yml_middle = (yml == True  and line.strip() not in (yml_beg, yml_end))
            check_yml_end    = (yml == True  and line.strip() == yml_end and ymlcnt == 0)
            if (check_yml_begin or yml):
                # print check_yml_begin, check_yml_middle, check_yml_end
                # Flush previous code buffers
                if (yml is True and line.strip() == yml_beg):
                    ymlcnt += 1
                if (not check_yml_end and line.strip() == yml_end):
                    ymlcnt += 1
                if check_yml_begin and curcode:
                    for item in do_insertions(insertions, lualexer.get_tokens_unprocessed(curcode)):
                        yield item
                    code = False
                    curcode = ''
                    insertions = []
                curyml += line
                # We finished reading YAML output, so push it to user
                if check_yml_end:
                    for item in ymllexer.get_tokens_unprocessed(curyml):
                        yield item
                    curyml = ''
                yml = False if check_yml_end else True
                # print 'yaml gotcha %d' % yml
                continue

            # Third part - check for Tarantool's Lua
            # It's combination of:
            # prompt: tarantool> or localhost> or localhost:{port}>
            # lua: body after prompt + space
            prompt_pos_flexible = find_prompt(line)
            prompt_pos_strict   = prompt_pos_flexible if not code else None
            if prompt_pos_strict:
                prompt_len = prompt_pos_strict + 2

            check_code_begin = bool(prompt_pos_strict)
            check_code_middle = code and line.startswith(' ' * (prompt_len - 2) + '> ')
            check_code_flexible = False
            # e.g. we have two 'tarantool> ' in a row - code is True and
            # check_code_middle is False then we have to do something about it,
            # otherwise it will be like Generic.Output
            if code and check_code_middle is False and bool(prompt_pos_flexible):
                prompt_len = prompt_pos_flexible + 2
                check_code_flexible = True
            if (check_code_begin or check_code_middle or check_code_flexible):
                code = True
                insertions.append((len(curcode), [(0, Generic.Prompt, line[:prompt_len])]))
                curcode += line[prompt_len:]
                continue

            # If it's not something before - then we must check for code
            # and push that line as 'Generic.Output'
            if curcode:
                for item in do_insertions(insertions, lualexer.get_tokens_unprocessed(curcode)):
                    yield item
                code = False
                curcode = ''
                insertions = []
            yield match.start(), Generic.Output, line

        if curcode:
            for item in do_insertions(insertions, lualexer.get_tokens_unprocessed(curcode)):
                yield item
        if curyml:
            for item in ymllexer.get_tokens_unprocessed(curyml):
                yield item
        if curshs:
            for item in shslexer.get_tokens_unprocessed(curshs):
                yield item

def setup(app):
    app.add_lexer("tarantoolsession", TarantoolSessionLexer())
