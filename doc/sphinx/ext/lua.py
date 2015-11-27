# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Previously Haka Domain (https://github.com/haka-security/haka)
# Written by Haka team

import re

from docutils import nodes
from docutils.parsers.rst import directives

from sphinx import addnodes
from sphinx.roles import XRefRole
from sphinx.locale import l_, _
from sphinx.domains import Domain, ObjType, Index
from sphinx.directives import ObjectDescription
from sphinx.util.nodes import make_refnode
from sphinx.util.compat import Directive
from sphinx.util.docfields import Field, GroupedField, TypedField
from sphinx.writers.html import HTMLTranslator


# Fix parameter display when using something different than '()'

def new_visit_desc_parameterlist(self, node):
    if hasattr(node, 'param_class'):
        param_class = ' class="param_start_%s"' % node.param_class
    else:
        param_class = ''

    if hasattr(node, 'param_start'):
        value = node.param_start
    else:
        value = '('

    self.body.append('<big%s>%s</big>' % (param_class, value))
    self.first_param = 1
    self.optional_param_level = 0
    # How many required parameters are left.
    self.required_params_left = sum([isinstance(c, addnodes.desc_parameter)
                                     for c in node.children])
    self.param_separator = node.child_text_separator

def new_depart_desc_parameterlist(self, node):
    if hasattr(node, 'param_class'):
        param_class = ' class="param_end_%s"' % node.param_class
    else:
        param_class = ''

    if hasattr(node, 'param_end'):
        value = node.param_end
    else:
        value = ')'

    self.body.append('<big%s>%s</big>' % (param_class, value))

HTMLTranslator.visit_desc_parameterlist = new_visit_desc_parameterlist
HTMLTranslator.depart_desc_parameterlist = new_depart_desc_parameterlist

def _desc_parameterlist(argstart, argend):
    node = addnodes.desc_parameterlist()
    node.param_start = argstart
    node.param_end = argend
    return node

def _pseudo_parse_arglist(signode, argstart, arglist, argend):
    """"Parse" a list of arguments separated by commas.

    Arguments can have "optional" annotations given by enclosing them in
    brackets.  Currently, this will split at any comma, even if it's inside a
    string literal (e.g. default argument value).
    """
    paramlist = _desc_parameterlist(argstart, argend)
    stack = [paramlist]
    for argument in arglist.split(','):
        argument = argument.strip()
        ends_open = ends_close = 0
        while argument.startswith('['):
            stack.append(addnodes.desc_optional())
            stack[-2] += stack[-1]
            argument = argument[1:].strip()

        while argument.startswith(']'):
            stack.pop()
            argument = argument[1:].strip()

        while argument.endswith(']'):
            ends_close += 1
            argument = argument[:-1].strip()

        while argument.endswith('['):
            ends_open += 1
            argument = argument[:-1].strip()

        if argument:
            stack[-1] += addnodes.desc_parameter(argument, argument)
            while ends_open:
                stack.append(addnodes.desc_optional())
                stack[-2] += stack[-1]
                ends_open -= 1

            while ends_close:
                stack.pop()
                ends_close -= 1

    if len(stack) != 1:
        raise IndexError

    signode += paramlist


# Lua objects

class LuaObject(ObjectDescription):
    """
    Description of a general Lua object.
    """
    option_spec = {
        'noindex': directives.flag,
        'annotation': directives.unchanged,
        'module': directives.unchanged,
        'objtype': directives.unchanged,
        'idxtype': directives.unchanged,
        'idxctx': directives.unchanged,
    }

    lua_signature_re = re.compile(
        r'''^ ([\w\.\:/\-]+[:.])?     # class name(s)
              ([<>\w/\-/]+)  \s*        # thing name
              (?: ([({])(.*)([)}]))?  # optional: arguments
              (?:\s* -> \s* (.*))?    # optional: return annotation
              $                       # and nothing more
              ''', re.VERBOSE)

    def needs_arglist(self):
        """May return true if an empty argument list is to be generated even if
        the document contains none."""
        return False

    def needs_module(self):
        """May return true if the module name should be displayed."""
        return self.context == None

    def needs_class(self):
        """May return true if the module name should be displayed."""
        return self.context == None

    def build_objtype(self):
        return self.options.get('objtype') or "%s" % (self.__class__.typename)

    def build_context(self, context):
        if context:
            return context[:-1], context[-1]
        else:
            return None, None

    def parse_signature(self, sig):
        m = self.__class__.lua_signature_re.match(sig)
        if m is None:
            raise ValueError

        return m.groups()

    def build_parameters(self, signode):
        if not self.arglist:
            if self.needs_arglist():
                # for callables, add an empty parameter list
                listnode = _desc_parameterlist(self.argstart, self.argend)
                signode += listnode
        else:
            _pseudo_parse_arglist(signode, self.argstart, self.arglist, self.argend)

    def build_signode(self, signode):
        if self.context:
            context = self.context + self.contextsep
            signode += addnodes.desc_addname(context, context)

        signode += addnodes.desc_name(self.name, self.name)
        self.build_parameters(signode)

        if self.retann:
            signode += addnodes.desc_returns(self.retann, self.retann)

    def handle_signature(self, sig, signode):
        a = (context, name, argstart, arglist, argend, retann) = self.parse_signature(sig)

        self.context, self.contextsep = self.build_context(context)
        self.module = self.options.get('module', self.env.temp_data.get('lua:module'))
        self.clsname = self.options.get('class', self.env.temp_data.get('lua:class'))
        self.objtype = self.build_objtype()
        self.idxtype = self.options.get('idxtype') or self.options.get('objtype')
        self.name = name
        self.argstart = argstart
        self.arglist = arglist
        self.argend = argend
        self.retann = retann
        # print "context: '%s', name: '%s'" % a[:2]
        # print "module: '%s', clsname: '%s', idxtype: '%s'" % (self.module, self.clsname, self.idxtype)
        # print "typename: '%s', objtype: '%s'" % (self.typename, self.objtype)
        # print "needs_module: '%s', needs_class: '%s'" % (self.needs_class(), self.needs_module())
        # print ""

        add_module = True
        fullname = name

        signode['module'] = self.module
        signode['class'] = self.context
        signode['fullname'] = fullname

        prefix = "%s " % (self.objtype)
        signode += addnodes.desc_annotation(prefix, prefix)

        clsname = ''
        if fullname.find('.') != -1 or str(self.typename) == "object":
            pass
        elif self.clsname and self.needs_class():
            sprtr = '.' if str(self.typename) == "data" else ':'
            clsname = '%s%s' % (self.clsname, sprtr)
        elif self.module and self.needs_module():
            clsname = '%s.' % (self.module)

        if clsname:
            signode += addnodes.desc_addname(clsname, clsname)

#        if self.clsname and self.needs_class():
#            signode += addnodes.desc_addname(clsname, clsname)
#        elif str(self.typename) == "data":
#            clsname = '%s:' % (self.clsname)
#            signode += addnodes.desc_addname(clsname, clsname)
#        elif self.module and self.needs_module():
#            modname = '%s.' % (self.module)
#            signode += addnodes.desc_addname(modname, modname)

        self.build_signode(signode)

        anno = self.options.get('annotation')
        if anno:
            signode += addnodes.desc_annotation(' ' + anno, ' ' + anno)

        return {'fullname': fullname, 'context': self.context,
                'objtype': self.objtype, 'idxctx': self.options.get('idxctx') or ""}

    def add_target_and_index(self, names, sig, signode):
        idxctx = self.options.get('idxctx')

        ids = ['lua-%s' % (self.__class__.typename)]
        if idxctx: ids.append(idxctx)
        if self.context: ids.append(self.context)
        elif self.clsname and self.needs_class(): ids.append(self.clsname)
        elif self.module and self.needs_module(): ids.append(self.module)
        ids.append(names['fullname'])

        fullid = '.'.join(ids)
#        self.state_machine.reporter.warning(fullid)

        fullname = []
        if self.clsname and self.needs_class(): fullname.append(self.clsname)
        elif self.module and self.needs_module(): fullname.append(self.module)
        if self.context: fullname.append(self.context)
        fullname.append(names['fullname'])
        fullname = '.'.join(fullname)

        # We need to escape the '<>' to avoid display issue in HTML
        fullname = fullname.replace("<", "&lt;")
        fullname = fullname.replace(">", "&gt;")

        if fullid not in self.state.document.ids:
            objtype = self.objtype
            if objtype == "": objtype = "%s" % (self.__class__.typename)

            signode['names'].append(fullname)
            signode['ids'].append(fullid)
            signode['first'] = (not self.names)
            self.state.document.note_explicit_target(signode)
            objects = self.env.domaindata['lua']['objects']
            objects[fullname] = (self.env.docname, objtype, fullid)

        indextext = self.get_index_text(names)
        if indextext:
            self.indexnode['entries'].append(('single', indextext,
                                              fullid, ''))

    def get_index_name(self, names):
        return names['fullname']

    def get_index_type(self):
        return None

    def get_index_text(self, names):
        ret = []

        idxtype = self.idxtype or self.get_index_type()
        if idxtype: ret.append(idxtype)

        if self.context: ret.append("in %s" % (self.context))
        if self.module and self.needs_module(): ret.append("in module %s" % (self.module))

        return "%s (%s)" % (self.get_index_name(names), ' '.join(ret))


class LuaClass(LuaObject):
    doc_field_types = [
        Field('extend', label=l_('Extends'), has_arg=False,
              names=('extend',)),
    ]

    typename = l_("object")

    def get_index_type(self):
        return "%s" % (self.__class__.typename)

    def before_content(self):
        LuaObject.before_content(self)
        if self.names:
            self.env.temp_data['lua:class'] = self.names[0]['fullname']

    def after_content(self):
        LuaObject.after_content(self)
        if self.names:
            self.env.temp_data['lua:class'] = None

class LuaFunction(LuaObject):
    typename = l_("function")

    doc_field_types = [
        TypedField('parameter', label=l_('Parameters'),
                   names=('param', 'parameter', 'arg', 'argument'),
                   typerolename='obj', typenames=('paramtype', 'type', 'ptype')),
        TypedField('returnvalues', label=l_('Returns'),
                  names=('return', 'ret'), typerolename='obj',
                  typenames=('rtype', 'type')),
        Field('returnvalue', label=l_('Returns'), has_arg=False,
              names=('returns')),
        Field('returntype', label=l_('Return type'), has_arg=False,
              names=('returntype',)),
    ]

    def build_objtype(self):
        return self.options.get('objtype') or ""

    def needs_arglist(self):
        return True

    def get_index_name(self, names):
        return '%s()' % (names['fullname'])

class LuaMethod(LuaFunction):
    option_spec = dict(
        abstract=directives.flag,
        **LuaObject.option_spec
    )

    def build_objtype(self):
        if 'abstract' in self.options:
            return "abstract %s" % (self.options.get('objtype') or "")
        else:
            return self.options.get('objtype') or ""


    def build_context(self, context):
        if context:
            return "%s" % (context[:-1]), context[-1]
        else:
            return None, None

class LuaData(LuaObject):
    typename = l_("data")

    option_spec = dict(
        readonly=directives.flag,
        **LuaObject.option_spec
    )

    doc_field_types = [
        Field('type', label=l_('Type'), has_arg=False,
              names=('type',)),
    ]

    def build_objtype(self):
        if 'readonly' in self.options:
            return "const %s" % (self.options.get('objtype') or "")
        else:
            return self.options.get('objtype') or ""

class LuaAttribute(LuaData):
    typename = l_("attribute")

    lua_class_re = re.compile(
        r'''([\w\./\-]+):([\w\./\-]+)?
        ''', re.VERBOSE)

    def build_context(self, context):
        if context:
            m = LuaAttribute.lua_class_re.match(context[:-1])
            if m:
                classname, subcontext = m.groups()
                if subcontext:
                    return "<%s>.%s" % (classname, subcontext), '.'
                else:
                    return "<%s>" % (classname), '.'
            else:
                return "<%s>" % (context[:-1]), '.'
        else:
            return None, None

class LuaOperator(LuaObject):
    typename = l_("operator")

    doc_field_types = [
        TypedField('parameter', label=l_('Parameters'),
                   names=('param', 'parameter', 'arg', 'argument'),
                   typerolename='obj', typenames=('paramtype', 'type', 'ptype')),
        TypedField('returnvalues', label=l_('Returns'),
                  names=('return', 'ret'), typerolename='obj',
                  typenames=('rtype', 'type')),
        Field('returnvalue', label=l_('Returns'), has_arg=False,
              names=('returns')),
        Field('returntype', label=l_('Return type'), has_arg=False,
              names=('returntype',)),
    ]

    lua_signature_unary_re = re.compile(
        r'''^ ([\+\-\*/<>=\#]+) \s*   # operator
              ([\w\./\-]+)            # class name(s)
              (?:\s* -> \s* (.*))?    # optional: return annotation
              $                       # and nothing more
              ''', re.VERBOSE)

    lua_signature_binary_re = re.compile(
        r'''^ ([\w\./\-]+)            # class name(s)
              \s* ([\+\-\*/<>=]+) \s* # operator
              ([\w\./\-]+)?           # class name(s)
              (?:\s* -> \s* (.*))?    # optional: return annotation
              $                       # and nothing more
              ''', re.VERBOSE)

    lua_signature_index_re = re.compile(
        r'''^ ([\w\./\-]+)            # class name(s)
              (\[)(.*)(\])            # arguments
              (?:\s* -> \s* (.*))?    # optional: return annotation
              $                       # and nothing more
              ''', re.VERBOSE)

    lua_signature_newindex_re = re.compile(
        r'''^ ([\w\./\-]+)            # class name(s)
              (\[)(.*)(\])            # arguments
              \s* = \s* (.*)          # return annotation
              $                       # and nothing more
              ''', re.VERBOSE)

    lua_signature_convert_re = re.compile(
        r'''^ ([\w/\-/]+) \s*         # thing name
              \( ([\w\./\-]+) \)      # class name(s)
              (?:\s* -> \s* (.*))?    # optional: return annotation
              $                       # and nothing more
              ''', re.VERBOSE)

    def parse_signature(self, sig):
        m = LuaOperator.lua_signature_unary_re.match(sig)
        if m:
            name, context, retann = m.groups()
            self.type = 'unary'
            return context, name, None, None, None, retann

        m = LuaOperator.lua_signature_binary_re.match(sig)
        if m:
            context, name, _, retann = m.groups()
            self.type = 'binary'
            return context, name, None, None, None, retann

        m = LuaOperator.lua_signature_index_re.match(sig)
        if m:
            context, argstart, arglist, argend, retann = m.groups()
            self.type = 'index'
            return context, '[]', argstart, arglist, argend, retann

        m = LuaOperator.lua_signature_newindex_re.match(sig)
        if m:
            context, argstart, arglist, argend, retann = m.groups()
            self.type = 'newindex'
            return context, '[]', argstart, arglist, argend, retann

        m = LuaOperator.lua_signature_convert_re.match(sig)
        if m:
            name, context, retann = m.groups()
            self.type = 'convert'
            return context, name, None, None, None, retann

        raise ValueError

    def build_context(self, context):
        if context:
            return "<%s>" % (context), ''
        else:
            return None, None

    def build_objtype(self):
        return self.options.get('objtype') or ""

    def build_signode(self, signode):
        if self.type == 'unary':
            signode += addnodes.desc_name(self.name, self.name)

            context = self.context + self.contextsep
            signode += addnodes.desc_addname(context, context)

            if self.retann:
                signode += addnodes.desc_returns(self.retann, self.retann)

        elif self.type == 'binary':
            context = self.context + self.contextsep
            name = " %s " % (self.name)

            signode += addnodes.desc_addname(context, context)
            signode += addnodes.desc_name(name, name)
            signode += addnodes.desc_addname(context, context)

            if self.retann:
                signode += addnodes.desc_returns(self.retann, self.retann)

        elif self.type == 'index' or self.type == 'newindex':
            context = self.context + self.contextsep
            signode += addnodes.desc_addname(context, context)

            self.build_parameters(signode)

            if self.retann:
                if self.type == 'newindex':
                    retann = " = %s" % (self.retann)
                    signode += addnodes.desc_type(retann, retann)
                else:
                    signode += addnodes.desc_returns(self.retann, self.retann)

        elif self.type == 'convert':
            context = self.context + self.contextsep

            signode += addnodes.desc_name(self.name, self.name)

            paramlist = _desc_parameterlist('(', ')')
            paramlist.append(addnodes.desc_addname(context, context))
            signode.append(paramlist)

            if self.retann:
                signode += addnodes.desc_returns(self.retann, self.retann)



class LuaModule(Directive):
    """
    Directive to mark description of a new module.
    """

    has_content = False
    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = False
    option_spec = {
        'platform': lambda x: x,
        'synopsis': lambda x: x,
        'noindex': directives.flag,
        'deprecated': directives.flag,
    }

    def run(self):
        env = self.state.document.settings.env
        modname = self.arguments[0].strip()
        noindex = 'noindex' in self.options
        env.temp_data['lua:module'] = modname
        ret = []
        if not noindex:
            env.domaindata['lua']['modules'][modname] = \
                (env.docname, self.options.get('synopsis', ''),
                 self.options.get('platform', ''), 'deprecated' in self.options)

            ids = "lua-module.%s" % (modname)

            # make a duplicate entry in 'objects' to facilitate searching for
            # the module in LuaDomain.find_obj()
            env.domaindata['lua']['objects'][modname] = (env.docname, 'module', ids)
            targetnode = nodes.target('', '', ids=[ids],
                                      ismod=True)
            self.state.document.note_explicit_target(targetnode)
            # the platform and synopsis aren't printed; in fact, they are only
            # used in the modindex currently
            ret.append(targetnode)
            indextext = _('%s (module)') % modname
            inode = addnodes.index(entries=[('single', indextext,
                                             ids, '')])
            ret.append(inode)
        return ret

class LuaCurrentModule(Directive):
    """
    This directive is just to tell Sphinx that we're documenting
    stuff in module foo, but links to module foo won't lead here.
    """
    has_content = False
    required_arguments = 1
    optional_arguments = 0
    final_argument_whitespace = False
    option_spec = {}

    def run(self):
        env = self.state.document.settings.env
        modname = self.arguments[0].strip()
        if modname == 'None':
            env.temp_data['lua:module'] = None
        else:
            env.temp_data['lua:module'] = modname
        return []


class LuaXRefRole(XRefRole):
    def process_link(self, env, refnode, has_explicit_title, title, target):
        refnode['lua:module'] = env.temp_data.get('lua:module')
        refnode['lua:class'] = env.temp_data.get('lua:class')
        if not has_explicit_title:
            title = title.lstrip('.')   # only has a meaning for the target
            target = target.lstrip('~') # only has a meaning for the title
            # if the first character is a tilde, don't display the module/class
            # parts of the contents
            if title[0:1] == '~':
                title = title[1:]
                dot = max(title.rfind('.'), title.rfind(':'))
                if dot != -1:
                    title = title[dot+1:]
        # if the first character is a dot, search more specific namespaces first
        # else search builtins first
        if target[0:1] == '.':
            target = target[1:]
            refnode['refspecific'] = True
        return title, target

class LuaModuleIndex(Index):
    """
    Index subclass to provide the Lua module index.
    """
    name = 'modindex'
    localname = l_('Lua Module Index')
    shortname = l_('modules')

    def generate(self, docnames=None):
        content = {}
        # list of prefixes to ignore
        ignores = self.domain.env.config['modindex_common_prefix']
        ignores = sorted(ignores, key=len, reverse=True)
        # list of all modules, sorted by module name
        modules = sorted(self.domain.data['modules'].items(),
                         key=lambda x: x[0].lower())
        # sort out collapsable modules
        prev_modname = ''
        num_toplevels = 0
        for modname, (docname, synopsis, platforms, deprecated) in modules:
            if docnames and docname not in docnames:
                continue

            for ignore in ignores:
                if modname.startswith(ignore):
                    modname = modname[len(ignore):]
                    stripped = ignore
                    break
            else:
                stripped = ''

            # we stripped the whole module name?
            if not modname:
                modname, stripped = stripped, ''

            entries = content.setdefault(modname[0].lower(), [])

            package = modname.split('.')[0]
            if package != modname:
                # it's a submodule
                if prev_modname == package:
                    # first submodule - make parent a group head
                    entries[-1][1] = 1
                elif not prev_modname.startswith(package):
                    # submodule without parent in list, add dummy entry
                    entries.append([stripped + package, 1, '', '', '', '', ''])
                subtype = 2
            else:
                num_toplevels += 1
                subtype = 0

            qualifier = deprecated and _('Deprecated') or ''
            entries.append([stripped + modname, subtype, docname,
                            'module-' + stripped + modname, platforms,
                            qualifier, synopsis])
            prev_modname = modname

        # apply heuristics when to collapse modindex at page load:
        # only collapse if number of toplevel modules is larger than
        # number of submodules
        collapse = len(modules) - num_toplevels < num_toplevels

        # sort by first letter
        content = sorted(content.items())

        return content, collapse


# Lua domain

class LuaDomain(Domain):
    """Lua language domain."""
    name = 'lua'
    label = 'Lua'
    object_types = {
        'class':         ObjType(l_('class'),      'class',  'obj'),
        'attribute':     ObjType(l_('attribute'),  'data',   'obj'),
        'function':      ObjType(l_('function'),   'func',   'obj'),
        'method':        ObjType(l_('method'),     'func',   'obj'),
        'operator':      ObjType(l_('operator'),   'func',   'obj'),
        'module':        ObjType(l_('module'),     'mod',    'obj'),
        'data':          ObjType(l_('data'),       'data',   'obj'),
    }

    directives = {
        'class':           LuaClass,
        'function':        LuaFunction,
        'method':          LuaMethod,
        'operator':        LuaOperator,
        'data':            LuaData,
        'attribute':       LuaAttribute,
        'module':          LuaModule,
        'currentmodule':   LuaCurrentModule,
    }
    roles = {
        'data':  LuaXRefRole(),
        'func':  LuaXRefRole(fix_parens=True),
        'class': LuaXRefRole(),
        'mod':   LuaXRefRole(),
    }
    initial_data = {
        'objects': {},     # fullname -> docname, objtype
        'modules': {},     # modname -> docname, synopsis, platform, deprecated
        'inheritance': {}, # class -> [ derived ]
    }
    indices = [
        LuaModuleIndex,
    ]

    def clear_doc(self, docname):
        for fullname, (fn, _, _) in list(self.data['objects'].items()):
            if fn == docname:
                del self.data['objects'][fullname]
        for modname, (fn, _, _, _) in list(self.data['modules'].items()):
            if fn == docname:
                del self.data['modules'][modname]

    def find_obj(self, env, modname, classname, name, type, searchmode=0):
        # skip parens
        if name[-2:] == '()':
            name = name[:-2]

        if not name:
            return []

        name = name.replace("<", "&lt;")
        name = name.replace(">", "&gt;")

        objects = self.data['objects']
        matches = []

        newname = None
        if searchmode == 1:
            objtypes = self.objtypes_for_role(type)
            if modname and classname:
                fullname = modname + '.' + classname + '.' + name
                if fullname in objects and objects[fullname][1] in objtypes:
                    newname = fullname
            if not newname:
                if modname and modname + '.' + name in objects and \
                   objects[modname + '.' + name][1] in objtypes:
                    newname = modname + '.' + name
                elif name in objects and objects[name][1] in objtypes:
                    newname = name
                else:
                    # "fuzzy" searching mode
                    searchname = '.' + name
                    matches = [(oname, objects[oname]) for oname in objects
                               if oname.endswith(searchname)
                               and objects[oname][1] in objtypes]
        else:
            # NOTE: searching for exact match, object type is not considered
            if name in objects:
                newname = name
            elif type == 'mod':
                # only exact matches allowed for modules
                return []
            elif classname and classname + '.' + name in objects:
                newname = classname + '.' + name
            elif modname and modname + '.' + name in objects:
                newname = modname + '.' + name
            elif modname and classname and \
                     modname + '.' + classname + '.' + name in objects:
                newname = modname + '.' + classname + '.' + name
            # special case: builtin exceptions have module "exceptions" set
            elif type == 'exc' and '.' not in name and \
                 'exceptions.' + name in objects:
                newname = 'exceptions.' + name
            # special case: object methods
            elif type in ('func', 'meth') and '.' not in name and \
                 'object.' + name in objects:
                newname = 'object.' + name
        if newname is not None:
            matches.append((newname, objects[newname]))
        return matches

    def resolve_xref(self, env, fromdocname, builder,
                     type, target, node, contnode):

        modname = node.get('lua:module')
        clsname = node.get('lua:class')
        searchmode = node.hasattr('refspecific') and 1 or 0
        matches = self.find_obj(env, modname, clsname, target,
                                type, searchmode)

        if not matches:
            # If type is 'obj', we don't want to display any WARNING.
            # Otherwise, we have too many due to unknown type used
            # as parameter type in function description (number, string...).
            if type != "obj":
                env.warn_node(
                    'no target found for cross-reference '
                    '%r' % (target), node)
            return None
        elif len(matches) > 1:
            env.warn_node(
                'more than one target found for cross-reference '
                '%r: %s' % (target, ', '.join(match[0] for match in matches)),
                node)
        name, obj = matches[0]

        return make_refnode(builder, fromdocname, obj[0], obj[2],
                            contnode, name)

    def get_objects(self):
        for modname, info in self.data['modules'].items():
            yield (modname, modname, 'module', info[0], 'module-' + modname, 0)
        for refname, (docname, type, id) in self.data['objects'].items():
            yield (refname, refname, type, docname, id, 1)


def setup(app):
    app.add_domain(LuaDomain)
