#!/usr/bin/env python
# -*- coding: utf-8 -*- #
from __future__ import unicode_literals
import re

AUTHOR = u'Tarantool'
SITENAME = u'Tarantool - a NoSQL database in a Lua script'
SITEURL = 'http://tarantool.org'

PATH = 'content'
THEME = "theme"
TIMEZONE = 'Europe/Moscow'

DEFAULT_LANG = u'en'

TAGS_SAVE_AS = ''
TAG_SAVE_AS = ''

PLUGINS = ['plugins.documentation']

# Feed generation is usually not desired when developing
FEED_ALL_ATOM = None
CATEGORY_FEED_ATOM = None
TRANSLATION_FEED_ATOM = None

DEFAULT_PAGINATION = False

DOCS_PATH = ['docs']
ARTICLE_EXCLUDES = ['docs']

JINJA_FILTERS = {
        're_replace': (lambda s, i, o: re.sub(i, o, s)),
}

INDEX_SAVE_AS = ''
ARCHIVES_SAVE_AS = ''
AUTHORS_SAVE_AS = ''
CATEGORIES_SAVE_AS = ''
TAGS_SAVE_AS = ''

# Uncomment following line if you want document-relative URLs when developing
#RELATIVE_URLS = True
