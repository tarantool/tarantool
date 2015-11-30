# -*- coding: utf-8 -*-
import sys
import os

sys.path.insert(0, os.path.abspath('..'))

# -- General configuration ------------------------------------------------

master_doc = 'index'

extensions = [
    'sphinx.ext.todo',
    'sphinx.ext.ifconfig',
    #'sphinx.ext.autodoc',
    #'sphinx.ext.autosummary',
    'ext.custom',
    'ext.LuaDomain',
    'ext.LuaLexer',
    'ext.TapLexer',
    'ext.TarantoolSessionLexer',
    'breathe'
]
primary_domain = 'lua'
templates_path = ['../_templates']
source_suffix = '.rst'

project = u'Tarantool'
breathe_projects = {
    "api":"../../api/xml",
}

# |release| The full version, including alpha/beta/rc tags.
release = open('../../../VERSION').read().strip()
# |version| The short X.Y version.
version = '.'.join(release.split('.')[0:2])

exclude_patterns = [
    '_build',
    'book/connectors/__*',
    'book/replication/*-1.rst',
    'book/replication/*-2.rst',
    'book/configuration/cfg-*'
]

pygments_style = 'sphinx'

# -- Options for HTML output ----------------------------------------------
html_theme = 'classic'
html_theme_options = {
    'nosidebar': False
}
#html_logo = None
#html_favicon = None

html_static_path = ['../_static']
#html_extra_path = []
#html_additional_pages = {}

html_copy_source = True

html_use_index = True
html_show_sphinx = False
html_show_copyright = False
html_use_smartypants = False

# Tarantool custom roles
# Tarantool has extended Sphinx so that there are four new roles:
# :codenormal:`text`     displays text as monospace
# :codebold:`text`       displays text as monospace bold
# :codeitalic:`text`     displays text as monospace italic
# :codebolditalic:`text` displays text as monospace italic bold
# The effect on HTML output is defined in _static/sphinx_design.css
# (which is the css file designated in _templates/layout.html).
rst_prolog = """
.. role:: codenormal
   :class: ccode

.. role:: codebold
   :class: ccodeb

.. role:: codeitalic
   :class: ccodei

.. role:: codebolditalic
   :class: ccodebi

.. |nbsp| unicode:: 0xA0

"""

rst_epilog = """
.. |br| raw:: html

    <br />
"""

# def setup(sphinx):
#     sys.path.insert(0, os.path.abspath('./ext'))
#     from LuaLexer import LuaLexer
#     sphinx.add_lexer("lua_tarantool", LuaLexer())
#     from TarantoolSessionLexer import TarantoolSessionLexer
#     sphinx.add_lexer("tarantoolsession", TarantoolSessionLexer())
#     from TapLexer import TAPLexer
#     sphinx.add_lexer('tap', TAPLexer())
