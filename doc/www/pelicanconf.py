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

PLUGINS = ['plugins.documentation']

# Feed generation is usually not desired when developing
FEED_ALL_ATOM = None
CATEGORY_FEED_ATOM = None
TRANSLATION_FEED_ATOM = None

DEFAULT_PAGINATION = False

DOCS_PATH = ['doc']
ARTICLE_EXCLUDES = ['doc']

JINJA_FILTERS = {
        're_replace': (lambda s, i, o: re.sub(i, o, s)),
}

INDEX_SAVE_AS = ''
ARCHIVES_SAVE_AS = ''
AUTHORS_SAVE_AS = ''
CATEGORIES_SAVE_AS = ''
TAGS_SAVE_AS = ''
TAGS_SAVE_AS = ''
TAG_SAVE_AS = ''

STATIC_PATHS = [
    'robots.txt',
    'ycsb',
    'js/highcharts.js',
    'js/tabs.js'
]
EXTRA_PATH_METADATA = {
    'robots.txt'      : { 'path': 'robots.txt'   },
    'ycsb'            : { 'path': 'ycsb'         },
    'js/highcharts.js': { 'path': 'highcharts.js'},
    'js/tabs.js'      : { 'path': 'tabs.js'      },
}

# Uncomment following line if you want document-relative URLs when developing
#RELATIVE_URLS = True
