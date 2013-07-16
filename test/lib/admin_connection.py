__author__ = "Konstantin Osipov <kostja.osipov@gmail.com>"

# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
# OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
# HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
# LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
# OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
# SUCH DAMAGE.

import socket
import yaml
import sys
import re
from tarantool_connection import TarantoolConnection

#generation of "optional end" regexp
def re_optional_end(begin, opt_end):
    return begin + ''.join(map(lambda x: '(' + x, opt_end)) + (')?'*len(opt_end))
cr = re_optional_end

#generation of "two mandatory word, but second word may differ" regexp
def re_compose(begin, end):
    if end == None:
        return begin
    return begin+'\s*('+'|'.join(end)+')'

#simple server cmd's regexp generation
re_is_sim_admin_cmd = '\s*('+'|'.join([
    re_compose(cr('e', 'xit'), None),
    re_compose(cr('h', 'elp'), None),
    re_compose(cr('sh','ow'), [cr('in', 'fo'), cr('fi', 'ber'), 
        cr('co', 'nfiguration'), 'plugins', cr('sl', 'ab'), 
        cr('pa', 'lloc'), cr('st', 'at'), 'injections']
    ),
    re_compose(cr('re', 'load'), [cr('co', 'nfiguration')]),
    re_compose(cr('sa', 've'), [cr('co', 'redump'), cr('sn', 'apshot')])
])+')\s*$'

#beginnig of complex server cmd's regexp generation
re_is_com_admin_cmd = '\s*('+'|'.join([
    re_compose(cr('lu', 'a'), None), 
    re_compose(cr('se', 't'), [cr('in', 'jection')])
]) +')\s*'

#simple+complex admin cmd's regexp objects
is_admin_re_1 = re.compile(re_is_sim_admin_cmd, re.I)
is_admin_re_2 = re.compile(re_is_com_admin_cmd, re.I)

ADMIN_SEPARATOR = '\n'

class AdminConnection(TarantoolConnection):
    def execute_simple(self, command, silent, lua=False):
        if not command:
            return
        self.socket.sendall(('lua ' if lua else '') + command.replace('\n', ' ') + ADMIN_SEPARATOR)

        bufsiz = 4096
        res = ""

        while True:
            buf = self.socket.recv(bufsiz)
            if not buf:
                break
            res = res + buf
            if (res.rfind("\n...\n") >= 0 or res.rfind("\r\n...\r\n") >= 0):
                break

        # validate yaml by parsing it
        try:
            yaml.load(res)
        finally:
            if not silent:
                sys.stdout.write(command + ADMIN_SEPARATOR)
                sys.stdout.write(res.replace("\r\n", "\n"))
        return res

    def execute_no_reconnect(self, command, silent):
        add_lua = False
        rg1, rg2 = is_admin_re_1.match(command), is_admin_re_2.match(command)
        if (not rg1 or len(rg1.group()) != len(command)) and not rg2:
            add_lua=True
        return self.execute_simple(command, silent, lua=add_lua)
