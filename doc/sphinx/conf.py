# -*- coding: utf-8 -*-
import sys
import os

sys.path.insert(0, os.path.abspath('..'))

# -- General configuration ------------------------------------------------

master_doc = 'index'

extensions = [
    'sphinx.ext.todo',
    'sphinx.ext.ifconfig',
    'ext.custom',
    'ext.lua'
]
primary_domain = 'lua'
templates_path = ['../_templates']
source_suffix = '.rst'

project = u'Tarantool'

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

rst_epilog = """
.. |br| raw:: html

    <br />
"""
