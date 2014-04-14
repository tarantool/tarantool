#!/usr/bin/env python2
#
# This script is used to build www.tarantool.org
#
import os
import sys
import yaml
import shlex
import jinja2
import shutil
import fnmatch
import argparse
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
        self.doc         = 'doc/'
        self.doc_mpage   = 'mpage/'
        self.doc_opage   = 'user_guide.html'
        self.doc_css     = '../user/tnt.css'
        self.target      = ''


class Loader(object):
    def __init__(self, config):
        self.config = config
        self.texts = {}
        self.load_texts()
        self.environ = self.make_environ(config.layout_dir, self.texts)

    def load_texts(self):
        proc = subprocess.Popen(shlex.split('git rev-parse --abbrev-ref HEAD'), stdout=subprocess.PIPE)
        branch = proc.communicate()[0].strip()
        args ={
                'docpage': os.path.join(self.config.doc, self.config.doc_mpage, 'index.html').format(branch = branch),
                'branch' : branch
            }
        for i in os.listdir(self.config.text_dir):
            i = os.path.join(self.config.text_dir, i)
            with open(i, 'r') as f:
                self.texts.update(yaml.load(f.read()))
        for l, i in self.texts.iteritems():
            if isinstance(i, dict):
                for j, k in i.iteritems():
                    i[j] = markdown(k.format(**args), extensions=mdext)
            elif isinstance(i, basestring):
                self.texts[l] = markdown(i.format(**args), extensions=mdext)
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
        page = template.render({'page_name': name})
        self.write(filename, page)

    def render_pages(self):
        for name in self.texts:
            self.render(name)

    def gener_docs_header(self, branch, one_page):
        header = """
<div id="headwrap" class="columnlist">
    <div id="headl" class="column">{0}</div>
    <div id="headr" class="column">{1}</div>
</div>
    """

        lheader = """
### [Home](/) -> [Documentation][{ot}]

[one_page]: /doc/user_guide.html
[mul_page]: /doc/mpage/index.html """

        rheader = """
### [{type}][{t}]

[one_page]: /doc/user_guide.html
[mul_page]: /doc/mpage/index.html """

        env = {
            'type' : 'Page Per Chapter' if one_page else 'All in One Page',
            'ot'   : 'one_page' if one_page else 'mul_page',
            't'    : 'mul_page' if one_page else 'one_page',
        }
        lheader = markdown(lheader.format(**env), extensions=mdext)
        rheader = markdown(rheader.format(**env), extensions=mdext)

        return header.format(lheader, rheader)

    def render_docs(self):
        proc = subprocess.Popen(shlex.split('git rev-parse --abbrev-ref HEAD'), stdout=subprocess.PIPE)
        branch = proc.communicate()[0].strip()
        docs_template = self.environ.get_template('documentation')
        # ==========================================
        doc_mpath = os.path.join(self.config.doc, self.config.doc_mpage)
        doc_mpage_out = doc_mpath
        doc_mpath_out = os.path.join(self.config.output_path, doc_mpath)
        doc_mpath = os.path.join(self.config.input, doc_mpath)
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
                self.write(os.path.join(doc_mpage_out, i), data)
            shutil.copy(self.config.doc_css, doc_mpath_out)
        # ===========================================
        doc_opath = self.config.doc
        doc_opath_out = os.path.join(self.config.output_path, doc_opath)
        doc_opage_out = os.path.join(doc_opath, self.config.doc_opage)
        doc_opath = os.path.join(self.config.input, doc_opath)
        doc_opage = os.path.join(doc_opath, self.config.doc_opage)
        header = self.gener_docs_header(branch, True)
        if os.path.exists(doc_opage) and os.path.isfile(doc_opage):
            if not os.path.exists(doc_opath_out):
                os.makedirs(doc_opath_out)
            env = {'documentation': {'main'  : open(doc_opage).read(),
                                     'header': header},
                   'docs': 'True'}
            data = docs_template.render(env)
            self.write(doc_opage_out, data)
            shutil.copy(self.config.doc_css, doc_opath_out)
        # ===========================================

def parseArgs():
    cfg = MockConfig()
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', help='Folder with _text, _layout and doc catalogs', default='./', dest='input')
    parser.add_argument('--output', help='Folder for output (dafault is www-data)', default='../www-data', dest='output')
    parser.add_argument('--target', choices=['docs', 'site', ''], default='', dest='target')
    args = parser.parse_args()
    cfg.ouput_path = args.output
    cfg.layout_dir = os.path.join(args.input, cfg.layout_dir)
    cfg.text_dir = os.path.join(args.input, cfg.text_dir)
    cfg.doc_css = os.path.join(args.input, cfg.doc_css)
    cfg.input = args.input
    cfg.target = args.target
    return cfg

if __name__ == '__main__':
    cfg = parseArgs()
    loader = Loader(cfg)
    if not cfg.target or cfg.target == 'site':
        loader.render_pages()
        exit(0)
    elif cfg.target == 'docs':
        loader.render_docs()
        exit(0)
    else:
        exit(1)
