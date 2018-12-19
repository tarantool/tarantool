-- packagepath.lua
--
-- The purpose of package.setpath() is to be called from the
-- application entry point script to make relative imports and imports
-- from .rocks subdirectory possible. package.setpath() is recommended
-- to be called from entrypoint before any require() calls.
--
-- Without this if the app is started while current working directory
-- is not application root, relative require() and attempts to
-- require() lua packages installed to .rocks will fail, because all
-- require() calls will load modules relatively to current working
-- directory.

local fio = require('fio')

local function extend_path(path)
    package.path = package.path .. ';' .. path
end

local function extend_cpath(path)
    package.cpath = package.cpath .. ';' .. path
end

local function set_script_path()
    local script_filename = debug.getinfo(2, "S").source:sub(2)

    -- Absolute paths are required because the user can change working
    -- directory at runtime, for example when calling
    -- box.cfg{work_dir=...}  Without absolute base dir, the added
    -- load paths will become invalid.
    local script_dir = fio.abspath(script_filename:match("(.*/)") or './')

    extend_path(fio.pathjoin(script_dir, '/?.lua'))
    extend_path(fio.pathjoin(script_dir, '/?/init.lua'))
    extend_cpath(fio.pathjoin(script_dir, '/?.so'))

    extend_path(fio.pathjoin(script_dir, '/.rocks/share/tarantool/?.lua'))
    extend_path(fio.pathjoin(script_dir, '/.rocks/share/tarantool/?/init.lua'))
    extend_cpath(fio.pathjoin(script_dir, '/.rocks/lib/tarantool/?.so'))

    -- On OS X some rocks and apps ship shared libraries as .dylib,
    -- and others as .so. So we can't exclude .so by default, and just
    -- add .dylib to the list of options.
    if jit.os == "OSX" then
        extend_cpath(fio.pathjoin(script_dir, '/?.dylib'))
        extend_cpath(fio.pathjoin(script_dir, '/.rocks/lib/tarantool/?.dylib'))
    end
end

package.setpath = set_script_path
