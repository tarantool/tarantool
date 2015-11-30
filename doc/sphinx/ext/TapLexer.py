# -*- coding: utf-8 -*-
# This lexer was submitted to Pygments in December 2014 by Matt Layman.
# Because it has not yet been merged into the main Pygments project, it is
# included in tappy as an extension to Pygments.
"""
    pygments.lexers.tap
    ~~~~~~~~~~~~~~~~~~~

    Lexer for the Test Anything Protocol (TAP).

    :copyright: Copyright 2006-2014 by the Pygments team, see AUTHORS.
    :license: BSD, see LICENSE for details.
"""

from pygments.lexer import bygroups, RegexLexer
from pygments.token import Comment, Generic, Keyword, Name, Number, Text

__all__ = ['TAPLexer']


class TAPLexer(RegexLexer):
    """
    For Test Anything Protocol (TAP) output.

    .. versionadded:: 2.1
    """
    name = 'TAP'
    aliases = ['tap']
    filenames = ['*.tap']

    tokens = {
        'root': [
            # A TAP version may be specified.
            (r'^TAP version \d+\n', Name.Namespace),

            # Specify a plan with a plan line.
            (r'^1..\d+', Keyword.Declaration, 'plan'),

            # A test failure
            (r'^(not ok)([^\S\n]*)(\d*)',
             bygroups(Generic.Error, Text, Number.Integer), 'test'),

            # A test success
            (r'^(ok)([^\S\n]*)(\d*)',
             bygroups(Keyword.Reserved, Text, Number.Integer), 'test'),

            # Diagnostics start with a hash.
            (r'^#.*\n', Comment),

            # TAP's version of an abort statement.
            (r'^Bail out!.*\n', Generic.Error),

            # TAP ignores any unrecognized lines.
            (r'^.*\n', Text),
        ],
        'plan': [
            # Consume whitespace (but not newline).
            (r'[^\S\n]+', Text),

            # A plan may have a directive with it.
            (r'#', Comment, 'directive'),

            # Or it could just end.
            (r'\n', Comment, '#pop'),

            # Anything else is wrong.
            (r'.*\n', Generic.Error, '#pop'),
        ],
        'test': [
            # Consume whitespace (but not newline).
            (r'[^\S\n]+', Text),

            # A test may have a directive with it.
            (r'#', Comment, 'directive'),

            (r'\S+', Text),

            (r'\n', Text, '#pop'),
        ],
        'directive': [
            # Consume whitespace (but not newline).
            (r'[^\S\n]+', Comment),

            # Extract todo items.
            (r'(?i)\bTODO\b', Comment.Preproc),

            # Extract skip items.
            (r'(?i)\bSKIP\S*', Comment.Preproc),

            (r'\S+', Comment),

            (r'\n', Comment, '#pop:2'),
        ],
    }

def setup(app):
    app.add_lexer('tap', TAPLexer())
