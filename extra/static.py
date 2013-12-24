#!/usr/bin/env python
#
# This script is used to build www.tarantool.org
#
import os
import re
import sys
import yaml
import jinja2
import markdown
import argparse
import fnmatch
import glob
import shutil
import datetime


default_lang = {
    'source-encoding': 'utf-8',
    'output-encoding': 'utf-8',
    'suffix': '.html' }


class Config(object):

    def __init__(self):
        self.source_path = '.'
        self.config_file = '_config'
        self.ignore_file = '_ignore'
        self.corpus_dir = '.'
        self.layout_dir = '_layout'
        self.output_dir = 'www'
        self.abs_output_path = None
        self.config = {}

    def __getitem__(self, key):
        return self.config[key]

    @property
    def config_path(self):
        return os.path.join(self.source_path, self.config_file)
    @property
    def corpus_path(self):
        return os.path.join(self.source_path, self.corpus_dir)
    @property
    def layout_path(self):
        return os.path.join(self.source_path, self.layout_dir)
    @property
    def output_path(self):
        if self.abs_output_path:
            return self.abs_output_path
        return os.path.join(self.source_path, self.output_dir)

    @output_path.setter
    def output_path(self, value):
        self.abs_output_path = value

    def check(self, key, config):
        if config and key in config:
            value = config[key]
            print 'set %s to %s' % (key, value)
            setattr(self, key, value)
            del config[key]

    def load(self):
        f = open(self.config_path)
        config = yaml.load(f)
        f.close()
        self.check('layout_dir', config)
        self.check('output_dir', config)
        self.config = config
        #print config

    def check_ignore_dir(self, path, name, ignore_list):
        if fnmatch.fnmatch(name, '_*'):
            return True
        pathname = os.path.normpath(os.path.join(path, name))
        if pathname in ignore_list:
            return True
        return False

    def check_ignore_file(self, path, name, ignore_list):
        if fnmatch.fnmatch(name, '_*'):
            return True
        if fnmatch.fnmatch(name, '*~'):
            return True
        pathname = os.path.normpath(os.path.join(path, name))
        if pathname in ignore_list:
            return True
        return False

    def load_ignore_file(self, path, ignore_list):
        try:
            f = open(os.path.join(path, self.ignore_file))
            words = f.read().split()
            f.close()
            for word in words:
                pattern = os.path.normpath(os.path.join(path, word))
                names = glob.glob(pattern)
                ignore_list.extend(names)
        except:
            pass

    def get_corpus(self):
        list = []
        ignore_list = [ os.path.normpath(self.output_path) ]
        root = self.corpus_path
        walker = os.walk(root)
        for curdir, subdirs, files in walker:
            self.load_ignore_file(curdir, ignore_list)
            for s in subdirs[:]:
                if self.check_ignore_dir(curdir, s, ignore_list):
                    subdirs.remove(s)
            for f in files:
                if not self.check_ignore_file(curdir, f, ignore_list):
                    dir = curdir.replace(root, '')
                    list.append(os.path.normpath(os.path.join(dir, f)))
        #print list
        return list


class Scanner(object):

    HEAD_OPEN = r'{%'
    HEAD_CLOSE = r'%}'

    WS = re.compile(r'\s+')
    WORD = re.compile(r'\w+')
    BODY = re.compile(r'.*?(?=\s*^\s*%s)' % re.escape(HEAD_OPEN), re.M | re.S)

    def __init__(self, name, config):
        f = open(os.path.join(config.source_path, name))
        self.data = f.read()
        f.close()
        self.name = name
        self.pos = 0
        self.token = None

    def __iter__(self):
        return self

    def match(self, cre):
        m = cre.match(self.data, self.pos)
        if m:
            self.pos = m.end()
            self.token = m.group()
            return True
        return False

    def match_str(self, str):
        if self.data.startswith(str, self.pos):
            self.pos += len(str)
            self.token = str
            return True
        return False

    def read_tags(self):
        tags = []
        if self.match_str(self.HEAD_OPEN):
            self.match(self.WS)
            while self.match(self.WORD):
                tags.append(self.token)
                self.match(self.WS)
            if not self.match_str(self.HEAD_CLOSE):
                print self.data[self.pos:]
                raise RuntimeError()
        return tags

    def read_text(self):
        self.match(self.WS)
        if self.match(self.BODY):
            text = self.token
        else:
            text = self.data[self.pos:]
            self.pos = len(self.data)
        return text

    def next(self):
        self.match(self.WS)
        if self.pos == len(self.data):
            raise StopIteration
        return self.name, self.read_tags(), self.read_text()


@jinja2.contextfilter
def langselect(context, data):
    if isinstance(data, dict):
        lang = context['pagelang']
        if lang in data:
            data = data[lang]
        else:
            data = data['en']
    return data


def make_environ(path):
    env = jinja2.Environment(loader = jinja2.FileSystemLoader(path))
    env.filters['langselect'] = langselect
    env.globals['date'] = datetime.datetime.today()
    return env


class BaseHandler(object):

    def __init__(self, config):
        self.config = config

    def enter(self, entry):
        pass

    def render(self, entry):
        pass


class PageHandler(BaseHandler):

    def __init__(self, config):
        super(PageHandler, self).__init__(config)
        self.environ = make_environ(config.layout_path)

    def write(self, name, data):
        print 'Writing %s' % name
        f = open(os.path.join(self.config.output_path, name), 'w')
        f.write(data)
        f.close

    def render(self, entry):
        name, tags, text = entry
        if len(tags) < 2:
            raise StandardError('missing template name for page entry')
        layout = tags[1]
        lang = tags[2] if tags and len(tags) > 2 else Nil
        if lang and lang in self.config['languages']:
            langdesc = self.config['languages'][lang]
        else:
            langdesc = default_lang
        text = unicode(text, langdesc['source-encoding'])
        text = markdown.markdown(text, ['tables'])
        filename = name + langdesc['suffix']
        template = self.environ.get_template(layout, globals=self.config.config)
        page = template.render(
            content=text,
            filename=filename,
            pagename=name,
            pagelang=lang)
        self.write(filename, page.encode(langdesc['output-encoding']))


class DataHandler(BaseHandler):
    pass


class TextHandler(BaseHandler):

    def enter(self, entry):
        name, tags, text = entry
        if len(tags) < 2:
            raise StandardError('missing item name for text entry')
        item = tags[1]
        lang = tags[2] if tags and len(tags) > 2 else 'en'
        if lang and lang in self.config['languages']:
            langdesc = self.config['languages'][lang]
        else:
            langdesc = default_lang
        text = unicode(text, langdesc['source-encoding'])
        text = markdown.markdown(text)
        self.config.config.setdefault(item, {})[lang] = text;


class PostHandler(TextHandler):
    pass


class Renderer(object):

    def __init__(self, config):
        self.handlers = { 'page': PageHandler(config),
                          'data': DataHandler(config),
                          'text': TextHandler(config),
                          'post': PostHandler(config) }

    def get_handler(self, tags):
        entry_type = tags[0] if tags and len(tags) > 0 else 'page'
        if entry_type not in self.handlers:
            raise ValueError('bad entry type %s' % entry_type)
        return self.handlers[entry_type]

    def enter_entry(self, entry):
        handler = self.get_handler(entry[1])
        handler.enter(entry);

    def render_entry(self, entry):
        handler = self.get_handler(entry[1])
        handler.render(entry);

    def render(self, entries):
        for entry in entries:
            self.enter_entry(entry)
        for entry in entries:
            self.render_entry(entry)


def parse_args(config):
    parser = argparse.ArgumentParser()
    parser.add_argument('--config-file')
    parser.add_argument('--source-path')
    parser.add_argument('--output-path')
    parser.parse_args(namespace = config)

def load_entries(config):
    entries = []
    corpus = config.get_corpus()
    for name in corpus:
        scanner = Scanner(name, config)
        for entry in iter(scanner):
            entries.append(entry)
        print 'Loaded content file "%s"' % name
    return entries

def main():
    config = Config()
    parse_args(config)
    config.load()
    renderer = Renderer(config)
    entries = load_entries(config)
    renderer.render(entries)

if __name__ == '__main__':
    main()
