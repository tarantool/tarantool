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
    'breathe',
    'sphinx.ext.intersphinx',
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

.. role:: codegreen
   :class: ccodegreen

.. role:: codered
   :class: ccodered

.. role:: codeblue
   :class: ccodeblue

.. |nbsp| unicode:: 0xA0

"""

rst_epilog = """
.. |br| raw:: html

    <br />
"""

intersphinx_mapping = {
    'tarantoolc': ('http://tarantool.github.io/tarantool-c/', None)
}

intersphinx_cache_limit = 0
