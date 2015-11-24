from pelican import signals
from pelican.generators import Generator
from pelican.contents import Content, is_valid_content

import os
import glob
import logging
import itertools

import re
import yaml
import docutils.core
import collections

from BeautifulSoup import BeautifulSoup as BSHTML

logger = logging.getLogger(__name__)

class BeautifulSite(Content):
    mandatory_properties = ('title', )
    default_template = 'beautifulsite'

class BSiteContainer(object):
    def __init__(self, opage):
        pass

class BSiteGenerator(Generator):
    def __init__(self, *args, **kwargs):
        super(BSiteGenerator, self).__init__(*args, **kwargs)
        self.bsite = []

    def _doc_read_file(self, relpath):
        def travel_string_rst(obj):
            def fromrst_to_string(objstring):
                obj = docutils.core.publish_parts(source=objstring, writer_name='html')
                return obj['html_title'] + obj['fragment']
            if isinstance(obj, (list, tuple)):
                return [travel_string_rst(subobj) for subobj in obj]
            elif isinstance(obj, dict):
                if 'format' in obj and 'content' in obj:
                    html = fromrst_to_string(obj['content'])
                    leftb = html.rfind('<p>')
                    rightb = html.rfind('</p>')
                    if rightb == len(html) - 5:
                        html = html[:leftb] + html[leftb+3:rightb]
                    return html
                return {k: travel_string_rst(subobj) for k, subobj in obj.iteritems()}
            else:
                return obj

        page = yaml.load(open(os.path.join(self.path, relpath), 'r').read())
        relpath = re.sub('\.yml$', '.html', relpath)
        page = type('BeautifulSite', (object, ), {
                'get_relative_source_path': (lambda x: x.save_as),
                'blocks'  : travel_string_rst(page.get('blocks', None)),
                'title'   : page.get('title', "Tarantool"),
                'url'     : relpath,
                'save_as' : page.get('save_as', relpath),
                'template': page.get('template', 'beautifulsite'),
                'slug'    : page.get('slug', ''),
            })()
        self.add_source_path(page)
        return page

    def generate_context(self):
        def b_path(left, right):
            return os.path.join(left, os.path.basename(right))

        for docpath in self.settings['BSITE_PATH']:
            abspath = os.path.join(self.path, docpath, '*.yml')
            for op_abspath in glob.glob(abspath):
                op_relpath = b_path(docpath, op_abspath)
                if not os.path.isfile(op_abspath):
                    continue
                page = self._doc_read_file(op_relpath)
                self.bsite.append(page)

    def generate_output(self, writer):
        for page in self.bsite:
            writer.write_file(
                    page.save_as,
                    self.get_template(page.template),
                    self.context,
                    page = page)

def get_generators(pelican_object):
    return BSiteGenerator

def register():
    signals.get_generators.connect(get_generators)
