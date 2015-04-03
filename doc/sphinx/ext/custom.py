# -*- coding: utf-8 -*-
"""
    .ext.custom
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
    app.add_object_type('confval', 'confval',
                        objname='configuration value',
                        indextemplate='pair: %s; configuration value')
    app.add_object_type('errcode', 'errcode',
                        objname='error code value',
                        indextemplate='pair: %s; error code value')
    return {'version': '0.0.2', 'parallel_read_safe': True}
