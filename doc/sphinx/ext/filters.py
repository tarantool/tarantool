# -*- coding: utf-8 -*-
"""
    .ext.filters
    ~~~~~~~~~~~~
"""

import xml.etree.ElementTree as ET

def add_jinja_filters(app):
    app.builder.templates.environment.filters['cleantitle'] = (lambda x: ''.join(ET.fromstring('<p>'+x+'</p>').itertext()))

def setup(app):
    '''
    Adds extra jinja filters.
    '''
    app.connect("builder-inited", add_jinja_filters)
    return {'version': '0.0.1', 'parallel_read_safe': True}
