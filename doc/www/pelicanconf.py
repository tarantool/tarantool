#!/usr/bin/env python
# -*- coding: utf-8 -*- #
from __future__ import unicode_literals
import re
import urlparse

AUTHOR = u'Tarantool'
SITENAME = u'Tarantool - a NoSQL database running in a Lua application server'
SITEURL = 'http://tarantool.org'

PATH = 'content'
THEME = "theme"
TIMEZONE = 'Europe/Moscow'

DEFAULT_LANG = u'en'

PLUGINS = ['plugins.beautifulsite']

# Feed generation is usually not desired when developing
FEED_ALL_ATOM = None
CATEGORY_FEED_ATOM = None
TRANSLATION_FEED_ATOM = None

DEFAULT_PAGINATION = False

BSITE_PATH = ['newsite']
ARTICLE_EXCLUDES = ['doc', 'newsite']

JINJA_FILTERS = {
        're_replace': (lambda s, i, o: re.sub(i, o, s)),
        'url_split':  (lambda s: re.sub('www\.', '', urlparse.urlsplit(s).netloc))
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
    'js/ie8.js',
    'js/index_tabs.js',
    'js/bench_tabs.js',
    'js/main.js',
    'js/old_tabs.js',
    'js/select.js',
    'js/filesize.min.js'
]

EXTRA_PATH_METADATA = {}

# Uncomment following line if you want document-relative URLs when developing
#RELATIVE_URLS = True
