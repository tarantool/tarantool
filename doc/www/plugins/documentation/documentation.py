from pelican import signals
from pelican.generators import Generator
from pelican.contents import Content, is_valid_content

import os
import glob
import logging
import itertools

from BeautifulSoup import BeautifulSoup as BSHTML

logger = logging.getLogger(__name__)

class Documentation(Content):
    mandatory_properties = ('title', )
    default_template = 'documentation'

class DocumentationContainer(object):
    def __init__(self, opage):
        self.opage = opage
        self.mpage = []

    def add_mpage(self, mpage):
        self.mpage.append(mpage)

class DocumentationGenerator(Generator):
    def __init__(self, *args, **kwargs):
        super(DocumentationGenerator, self).__init__(*args, **kwargs)
        self.docs_html = []
        self.docs_rst = []

    def _doc_read_file(self, relpath, op=False):
        abspath = os.path.join(self.path, relpath)
        page = open(abspath, 'r').read()
        page = type('Documentation', (object, ), {
                'get_relative_source_path': (lambda x: x.save_as),
                'content' : page,
                'title'   : BSHTML(page).find('title').getText(),
                'url'     : relpath if op else os.path.dirname(relpath),
                'save_as' : relpath,
                'template': 'documentation'
            })()
        self.add_source_path(page)
        return page

    def generate_context(self):
        def b_path(left, right):
            return os.path.join(left,
                    os.path.basename(right))
        def db_path(left, right):
            return os.path.join(left,
                    os.path.basename(os.path.dirname(right)),
                    os.path.basename(right))

        for docpath in self.settings['DOCS_PATH']:
            abspath = os.path.join(self.path, docpath, '*.html')
            for op_abspath in glob.glob(abspath):
                op_relpath = b_path(docpath, op_abspath)
                if not os.path.isfile(op_abspath):
                    continue
                page = self._doc_read_file(op_relpath, True)
                self.docs_html.append(DocumentationContainer(page))
                if not os.path.isdir(op_abspath[:-5]):
                    continue
                mp_abspath = os.path.join(op_abspath[:-5], '*.html')
                for mp_html_abspath in glob.glob(mp_abspath):
                    mp_html_relpath = db_path(docpath, mp_html_abspath)
                    if not os.path.isfile(mp_html_abspath):
                        continue
                    page = self._doc_read_file(mp_html_relpath, False)
                    self.docs_html[-1].add_mpage(page)
        for docpath in self.settings['DOCS_PATH']:
            abspath = os.path.join(self.path, docpath, '*.rst')
            for op_abspath in glob.glob(abspath):
                op_relpath = b_path(docpath, op_abspath)
                if not os.path.isfile(op_abspath):
                    continue
                page = None
                try:
                    page = self.readers.read_file(
                            base_path = self.path, path = op_relpath,
                            content_class = Documentation, context = self.context)
                except Exception as e:
                    logger.error('Could not process %s\n%s', op_relpath, e,
                        exc_info=self.settings.get('DEBUG', False))
                    continue
                if not is_valid_content(page, op_relpath):
                    continue
                if page:
                    self.docs_rst.append(DocumentationContainer(page))

    def generate_output(self, writer):
        for doc_cont in self.docs_html:
            opage = doc_cont.opage
            writer.write_file(
                    opage.save_as,
                    self.get_template(opage.template),
                    self.context,
                    page = opage,
                    documentation = True)
            for mpage in doc_cont.mpage:
                writer.write_file(
                        mpage.save_as,
                        self.get_template(mpage.template),
                        self.context,
                        page = mpage,
                        documentation = True)
        for doc_cont in self.docs_rst:
            opage = doc_cont.opage
            writer.write_file(
                    opage.save_as,
                    self.get_template(opage.template),
                    self.context,
                    page = opage,
                    documentation = False)

def get_generators(pelican_object):
    return DocumentationGenerator

def register():
    signals.get_generators.connect(get_generators)
