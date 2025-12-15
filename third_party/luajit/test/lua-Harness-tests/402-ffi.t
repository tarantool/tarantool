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

=head1 FFI Library

=head2 Synopsis

    % prove 402-ffi.t

=head2 Description

See L<https://luajit.org/ext_ffi.html>.

=cut

--]]

require 'test_assertion'

if not jit then
    skip_all("only with LuaJIT")
end

if not pcall(require, 'ffi') then
    plan(2)
    is_nil(_G.ffi, "no FFI")
    is_nil(package.loaded.ffi)
    os.exit(0)
end

plan(33)

is_nil(_G.ffi, "ffi not loaded by default")
ffi = require'ffi'
equals(package.loaded.ffi, ffi, "package.loaded")
equals(require'ffi', ffi, "require")

do -- C
    is_userdata(ffi.C, 'C')
end

do -- abi
    is_boolean(ffi.abi('32bit'), "abi")
    is_boolean(ffi.abi('64bit'))
    is_boolean(ffi.abi('le'))
    is_boolean(ffi.abi('be'))
    is_boolean(ffi.abi('fpu'))
    is_boolean(ffi.abi('softfp'))
    is_boolean(ffi.abi('hardfp'))
    is_boolean(ffi.abi('eabi'))
    is_boolean(ffi.abi('win'))
    is_false(ffi.abi('bad'))
    is_false(ffi.abi(0))

    error_matches(function () ffi.abi(true) end,
            "^[^:]+:%d+: bad argument #1 to 'abi' %(string expected, got boolean%)",
            "function unpack missing size")
end

do -- alignof
    is_function(ffi.alignof, "alignof")
end

do -- arch
    equals(ffi.arch, jit.arch, "alias arch")
end

do -- cast
    is_function(ffi.cast, "cast")
end

do -- cdef
    is_function(ffi.cdef, "cdef")
end

do -- copy
    is_function(ffi.copy, "copy")
end

do -- errno
    is_function(ffi.errno, "errno")
end

do -- fill
    is_function(ffi.fill, "fill")
end

do -- gc
    is_function(ffi.gc, "gc")
end

do -- istype
    is_function(ffi.istype, "istype")
end

do -- load
    is_function(ffi.load, "load")
end

do -- metatype
    is_function(ffi.metatype, "metatype")
end

do -- new
    is_function(ffi.new, "new")
end

do -- offsetof
    is_function(ffi.offsetof, "offsetof")
end

do -- os
    equals(ffi.os, jit.os, "alias os")
end

do -- sizeof
    is_function(ffi.sizeof, "sizeof")
end

do -- string
    is_function(ffi.string, "string")
end

do -- typeof
    is_function(ffi.typeof, "typeof")
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
