
# -*- coding: utf-8 -*-

from pygments.lexer import Lexer, RegexLexer, include, bygroups, using, \
    default, words, combined, do_insertions
from pygments.util import get_bool_opt, shebang_matches
from pygments.token import Text, Comment, Operator, Keyword, Name, String, \
    Number, Punctuation, Generic, Other, Error
from pygments import unistring as uni

from LuaLexer import LuaLexer
from pygments.lexers import YamlLexer

import re

__all__ = ['TarantoolSessionLexer']

line_re = re.compile('.*?\n')

prompt_beg = u"tarantool> "
prompt_con = u"         > "
prompt_len = len(prompt_beg)

yml_beg = u"---"
yml_end = u"..."

class TarantoolSessionLexer(Lexer):
    """
    For Tarantol session output:

    .. sourcecode:: tarantoolsession

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

        curcode = ''
        insertions = []
        curyml = ''
        yml = 0

        for match in line_re.finditer(text):
            line = match.group()
            if not yml and (line.startswith(prompt_beg) or \
                    line.startswith(prompt_con)):
                insertions.append((len(curcode),
                                   [(0, Generic.Prompt, line[:prompt_len])]))
                curcode += line[prompt_len:]
            elif not yml and line.strip() == yml_beg:
                yml = 1
                if curcode:
                    for item in do_insertions(
                            insertions, lualexer.get_tokens_unprocessed(curcode)):
                        yield item
                    curcode = ''
                    insertions = []
                curyml = line
            elif yml and line.strip() == yml_end:
                # Process end of YAML block
                curyml += line
                for item in ymllexer.get_tokens_unprocessed(curyml):
                    yield item
                yml = 0
                curyml = ''
            elif yml:
                # Process begin or content of YAML block
                yml = 1
                curyml += line
#                for item in ymllexer.get_tokens_unprocessed(curyml):
#                    yield item
            else:
                if curcode:
                    for item in do_insertions(
                            insertions, lualexer.get_tokens_unprocessed(curcode)):
                        yield item
                    curcode = ''
                    insertions = []
                yield match.start(), Generic.Output, line
        if curcode:
            for item in do_insertions(insertions,
                                      lualexer.get_tokens_unprocessed(curcode)):
                yield item
        if curyml:
            for item in ymllexer.get_token_unprocessed(curyml):
                yield item

