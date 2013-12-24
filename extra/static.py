#!/usr/bin/env python
import os
import sys
import yaml
import shlex
import jinja2
import shutil
import fnmatch
import subprocess

from markdown import markdown

mdext = [
        'codehilite',
        'fenced_code',
        'footnotes',
    ]


class MockConfig(object):
    def __init__(self):
        self.output_path = '../www-data/'
        self.layout_dir  = '_layout/'
        self.text_dir    = '_text/'
        self.doc         = 'doc/{branch}'
        self.doc_mpage   = 'mpage/'
        self.doc_opage   = 'user_guide.html'
        self.doc_css     = '../user/tnt.css'


class Loader(object):
    def __init__(self, config):
        self.config = config
        self.texts = {}
        self.load_texts()
        self.environ = self.make_environ(config.layout_dir, self.texts)

    def load_texts(self):
        for i in os.listdir(self.config.text_dir):
            i = os.path.join(self.config.text_dir, i)
            with open(i, 'r') as f:
                self.texts.update(yaml.load(f.read()))
        for l, i in self.texts.iteritems():
            if isinstance(i, dict):
                for j, k in i.iteritems():
                    i[j] = markdown(k, extensions=mdext)
            elif isinstance(i, basestring):
                self.texts[l] = markdown(i, extensions=mdext)
            else:
                raise Exception("hi")

    def make_environ(self, path, texts):
        env = jinja2.Environment(loader = jinja2.FileSystemLoader(path))
        env.globals.update(texts)
        return env

    def write(self, name, data):
        print 'Writing %s' % name
        with open(os.path.join(self.config.output_path, name), 'w') as f:
            f.write(data)

    def render(self, name):
        filename = name + '.html'
        template = self.environ.get_template(name)
        page = template.render()
        self.write(filename, page)

    def render_pages(self):
        for name in self.texts:
            self.render(name)

    def gener_docs_header(self, branch, one_page):
        assert(branch=='master' or branch=='stable')
        header = """
<div id="headwrap" class="columnlist">
    <div id="headl" class="column">{0}</div>
    <div id="headr" class="column">{1}</div>
</div>
    """
        lheader = """
### [Home](/) -> [Documentation](/doc/) """
        rheader = """
### [{bn}][{b}{o}] / [{bno}][{bo}{o}]

[mpdf]: /doc/master/user_guide.pdf
[mtxt]: /doc/master/user_guide.txt
[mopa]: /doc/master/user_guide.html
[mmpa]: /doc/master/mpage/index.html

[spdf]: /doc/stable/user_guide.pdf
[stxt]: /doc/stable/user_guide.txt
[sopa]: /doc/stable/user_guide.html
[smpa]: /doc/stable/mpage/index.html """

        env = {
            'b'    : branch[0],
            'bn'   : branch.capitalize(),
            'bo'   : 'm' if branch[0] == 's' else 's',
            'bno'  : ('master' if branch == 'stable' else 'stable').capitalize(),
            'o'    : 'mpa' if one_page else 'opa',
            'opage': 'Multiple pages' if one_page else 'One page',
        }

        return header.format(markdown(lheader), markdown(rheader.format(**env), extensions=mdext))

    def render_docs(self):
        proc = subprocess.Popen(shlex.split('git rev-parse --abbrev-ref HEAD'), stdout=subprocess.PIPE)
        branch = proc.communicate()[0].strip()
        docs_template = self.environ.get_template('documentation')
        # ==========================================
        doc_mpath = os.path.join(self.config.doc, self.config.doc_mpage).format(branch=branch)
        doc_mpath_out = os.path.join(self.config.output_path, doc_mpath)
        header = self.gener_docs_header(branch, False)
        if os.path.exists(doc_mpath):
            if not os.path.exists(doc_mpath_out):
                os.makedirs(doc_mpath_out)
            for i in os.listdir(doc_mpath):
                document = os.path.join(doc_mpath, i)
                env = {'documentation': {'main'  : open(document).read(),
                                         'header': header},
                       'docs': 'True'}
                data = docs_template.render(env)
                self.write(document, data)
            shutil.copy(self.config.doc_css, doc_mpath_out)
        # ===========================================
        doc_opath = self.config.doc.format(branch=branch)
        doc_opage = os.path.join(doc_opath, self.config.doc_opage)
        doc_opath_out = os.path.join(self.config.output_path, doc_opath)
        if os.path.exists(doc_opage) and os.path.isfile(doc_opage):
            if not os.path.exists(doc_opath_out):
                os.makedirs(doc_opath_out)
            env = {'documentation': {'main'  : open(doc_opage).read(),
                                     'header': header},
                   'docs': 'True'}
            data = docs_template.render(env)
            self.write(doc_opage, data)
            shutil.copy(self.config.doc_css, doc_opath_out)
        # ===========================================

if __name__ == '__main__':
    loader = Loader(MockConfig())
    if len(sys.argv) == 1 or sys.argv[1] == 'site':
        loader.render_pages()
        exit(0)
    elif sys.argv[1] == 'docs':
        loader.render_docs()
        exit(0)
    else:
        exit(1)
