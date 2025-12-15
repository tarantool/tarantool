#! /usr/bin/lua
--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2018-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

--[[

=head1 JIT Library

=head2 Synopsis

    % prove 403-jit.t

=head2 Description

See L<https://luajit.org/ext_jit.html>.

=cut

--]]

require 'test_assertion'
local profile = require'profile'

if not jit then
    skip_all("only with LuaJIT")
end

local compiled_with_jit = jit.opt ~= nil
local luajit20 = jit.version_num < 20100 and not jit.version:match'RaptorJIT'
local has_jit_opt = compiled_with_jit
local has_jit_security = jit.security
local has_jit_util = not ujit and not jit.version:match'RaptorJIT'

plan'no_plan'

equals(package.loaded.jit, _G.jit, "package.loaded")
equals(require'jit', jit, "require")

do -- arch
    is_string(jit.arch, "arch")
end

do -- flush
    is_function(jit.flush, 'function', "flush")
end

do -- off
    jit.off()
    is_false(jit.status(), "off")
end

-- on
if compiled_with_jit then
    jit.on()
    is_true(jit.status(), "on")
else
    error_matches(function () jit.on() end,
            "^[^:]+:%d+: JIT compiler permanently disabled by build option",
            "no jit.on")
end

-- opt
if has_jit_opt then
    is_table(jit.opt, "opt.*")
    is_function(jit.opt.start, 'function', "opt.start")
else
    is_nil(jit.opt, "no jit.opt")
end

do -- os
    is_string(jit.os, "os")
end

-- prngstate
if profile.openresty then
    is_table(jit.prngstate(), "prngstate")
    local s1 = { 1, 2, 3, 4, 5, 6, 7, 8}
    is_table(jit.prngstate(s1))
    local s2 = { 8, 7, 6, 5, 4, 3, 2, 1}
    array_equals(jit.prngstate(s2), s1)
    array_equals(jit.prngstate(), s2)

    is_table(jit.prngstate(32), 'table', "backward compat")
    array_equals(jit.prngstate(5617), { 32, 0, 0, 0, 0, 0, 0, 0 })
    array_equals(jit.prngstate(), { 5617, 0, 0, 0, 0, 0, 0, 0 })

    error_matches(function () jit.prngstate(-1) end,
            "^[^:]+:%d+: bad argument #1 to 'prngstate' %(PRNG state must be an array with up to 8 integers or an integer%)")

    error_matches(function () jit.prngstate(false) end,
            "^[^:]+:%d+: bad argument #1 to 'prngstate' %(table expected, got boolean%)")
elseif jit.version:match'moonjit' then
    equals(jit.prngstate(), 0, "prngstate")
else
    is_nil(jit.prngstate, "no jit.prngstate");
end

-- security
if has_jit_security then
    is_function(jit.security, "security")
    is_number(jit.security('prng'), "prng")
    is_number(jit.security('strhash'), "strhash")
    is_number(jit.security('strid'), "stdid")
    is_number(jit.security('mcode'), "mcode")

    error_matches(function () jit.security('foo') end,
            "^[^:]+:%d+: bad argument #1 to 'security' %(invalid option 'foo'%)")
else
    is_nil(jit.security, "no jit.security")
end

do -- status
    local status = { jit.status() }
    is_boolean(status[1], "status")
    if compiled_with_jit then
        for i = 2, #status do
            is_string(status[i], status[i])
        end
    else
        equals(#status, 1)
    end
end

-- util
if has_jit_util then
    local jutil = require'jit.util'
    is_table(jutil, "util")
    equals(package.loaded['jit.util'], jutil)

    if luajit20 then
        equals(jit.util, jutil, "util inside jit")
    else
        is_nil(jit.util, "no util inside jit")
    end
else
    local r = pcall(require, 'jit.util')
    is_false(r, "no jit.util")
end

do -- version
    is_string(jit.version, "version")
    matches(jit.version, '^%w+ %d%.%d%.%d')
end

do -- version_num
    is_number(jit.version_num, "version_num")
    matches(string.format("%06d", jit.version_num), '^0[12]0[012]%d%d$')
end

done_testing()

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
