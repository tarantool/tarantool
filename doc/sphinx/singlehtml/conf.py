# -*- coding: utf-8 -*-
execfile('../conf.py')

master_doc = 'singlehtml'

html_theme_options['nosidebar'] = True

exclude_patterns += [
        '../index'
]
