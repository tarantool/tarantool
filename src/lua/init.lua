
-- init.lua -- internal file

local ffi = require('ffi')
local compat = require('internal.compat')

ffi.cdef[[
struct method_info;

struct type_info {
    const char *name;
    const struct type_info *parent;
    const struct method_info *methods;
};
double
tarantool_uptime(void);
typedef int32_t pid_t;
pid_t getpid(void);
void
tarantool_exit(int);
]]

local fio = require("fio")

local soext = (jit.os == "OSX" and "dylib" or "so")

local ROCKS_LIB_PATH = '.rocks/lib/tarantool'
local ROCKS_LUA_PATH = '.rocks/share/tarantool'
local LIB_TEMPLATES = { '?.'..soext }
local LUA_TEMPLATES = { '?.lua', '?/init.lua' }
local ROCKS_LIB_TEMPLATES = { ROCKS_LIB_PATH .. '/?.'..soext }
local ROCKS_LUA_TEMPLATES = { ROCKS_LUA_PATH .. '/?.lua', ROCKS_LUA_PATH .. '/?/init.lua' }

-- A wrapper with a unique name, used in the call stack traversing.
local function __tarantool__internal__require__wrapper__()
    -- This require() may be overloaded.
    return require('log.get_callstack')
end

-- Calculate how many times the standard require() function was overloaded.
-- E.g. if there are three user overloads, the call stack returned by
-- get_callstack will look like:
-- 7. the place where require('log') was called
-- 6. require('log') -- overloaded in this file
-- 5. __tarantool__internal__require__wrapper__
-- 4. require('log.get_callstack') -- overloaded in user module 3
-- 3. require('log.get_callstack') -- overloaded in user module 2
-- 2. require('log.get_callstack') -- overloaded in user module 1
-- 1. require('log.get_callstack') -- overloaded in this file
local function get_require_overload_count(stack)
    local wrapper_name = '__tarantool__internal__require__wrapper__'
    local count = 0
    for _, f in pairs(stack) do
        if f.name ~= nil and string.find(f.name, wrapper_name) ~= nil then
            return count + 1
        end
        count = count + 1
    end
    return 0
end

-- Return longest common subpath of two paths.
local function get_common_subpath(path1, path2)
    local result = '/'
    path1 = path1:split('/')
    path2 = path2:split('/')
    for i = 1, math.min(#path1, #path2) do
        if path1[i] == path2[i] then
            result = fio.pathjoin(result, path1[i])
        else
            goto finish
        end
    end
::finish::
    return result == '/' and '/' or result .. '/'
end

-- Strip a common part of cwd and path from the path.
local function strip_cwd_from_path(cwd, path)
    if not cwd or cwd:sub(1, 1) ~= '/' or
       not path or path:sub(1, 1) ~= '/' then
        return path
    end
    local common = get_common_subpath(cwd, path)
    return path:sub(#common + 1)
end

-- Obtain the module name from the file name by removing:
-- 1. builtin/ prefixes
-- 2. path prefixes contained in package.path, package.cpath
-- 3. subpaths to the current directory
-- 4. ROCKS_LIB_PATH, ROCKS_LUA_PATH
-- 5. /init.lua and .lua suffixes
-- and by replacing all `/` with `.`
local function module_name_from_filename(filename)
    local paths = package.path .. package.cpath
    local result = filename:gsub('builtin/', '')
    for path in paths:gmatch'/([^?]+)\\?' do
        -- Escape magic characters in path.
        path = path:gsub('([^%w])', '%%%1')
        result = result:gsub('^/' .. path, '')
    end
    result = result:gsub('/init.lua', '')
    result = result:gsub('%.lua', '')
    result = strip_cwd_from_path(fio.cwd(), result)
    result = result:gsub(ROCKS_LIB_PATH .. '/', '')
    result = result:gsub(ROCKS_LUA_PATH .. '/', '')
    result = result:gsub('/', '.')
    return result
end

-- Take the function level in the call stack as input and return
-- the name of the module in which the function is defined.
local function module_name_by_callstack_level(level)
    local info = debug.getinfo(level + 1)
    if info ~= nil and info.source:sub(1, 1) == '@' then
        local src_name = info.source:sub(2)
        return module_name_from_filename(src_name)
    end
    -- require('log') called from the interactive mode or `tarantool -e`
    return 'tarantool'
end

-- Return current call stack.
local function get_callstack()
    local i = 2
    local stack = {}
    local info = debug.getinfo(i)
    while info ~= nil do
        stack[i] = info
        i = i + 1
        info = debug.getinfo(i)
    end
    return stack
end

-- Overload the require() function to set the module name during require('log')
-- Note that the standard require() function may be already overloaded in a user
-- module, also there can be multiple overloads.
local real_require = require
_G.require = function(modname)
    if modname == 'log' then
        local callstack = __tarantool__internal__require__wrapper__()
        local overload_count = get_require_overload_count(callstack)
        local name = module_name_by_callstack_level(overload_count + 2)
        return real_require('log').new(name)
    elseif modname == 'log.get_callstack' then
        return get_callstack()
    end
    return real_require(modname)
end

local package_searchroot

local function searchroot()
    return package_searchroot or fio.cwd()
end

local function setsearchroot(path)
    if not path then
        -- Here we need to get this function caller's sourcedir.
        path = debug.sourcedir(3)
    elseif path == box.NULL then
        path = nil
    else
        assert(type(path) == 'string', 'Search root must be a string')
    end
    package_searchroot = path and fio.abspath(path)
end

dostring = function(s, ...)
    local chunk, message = loadstring(s)
    if chunk == nil then
        error(message, 2)
    end
    return chunk(...)
end

local fiber = require("fiber")
local function exit(code)
    code = (type(code) == 'number') and code or 0
    ffi.C.tarantool_exit(code)
    -- Make sure we yield even if the code after
    -- os.exit() never yields. After on_shutdown
    -- fiber completes, we will never wake up again.
    local TIMEOUT_INFINITY = 500 * 365 * 86400
    while true do fiber.sleep(TIMEOUT_INFINITY) end
end
rawset(os, "exit", exit)

local function uptime()
    return tonumber(ffi.C.tarantool_uptime());
end

local function pid()
    return tonumber(ffi.C.getpid())
end

local function mksymname(name)
    local mark = string.find(name, "-")
    if mark then name = string.sub(name, mark + 1) end
    return "luaopen_" .. string.gsub(name, "%.", "_")
end

local function load_lib(file, name)
    return package.loadlib(file, mksymname(name))
end

local function load_lua(file)
    return loadfile(file)
end

local function traverse_path(path)
    path = fio.abspath(path)
    local paths = { path }

    while path ~= '/' do
        path = fio.dirname(path)
        table.insert(paths, path)
    end

    return paths
end

-- Generate a search function, which performs searching through
-- templates setup in options.
--
-- @param path_fn function which returns a base path for the
--     resulting template
-- @param templates table with lua search templates
-- @param need_traverse bool flag which tells search function to
--     build multiple paths by expanding base path up to the
--     root ('/')
-- @return a searcher function which builds a path template and
--     calls package.searchpath
local function gen_search_func(path_fn, templates, need_traverse)
    assert(type(path_fn) == 'function', 'path_fn must be a function')
    assert(type(templates) == 'table', 'templates must be a table')

    return function(name)
        local path = path_fn() or '.'
        local paths = need_traverse and traverse_path(path) or { path }

        local searchpaths = {}

        for _, path in ipairs(paths) do
            for _, template in pairs(templates) do
                table.insert(searchpaths, fio.pathjoin(path, template))
            end
        end

        local searchpath = table.concat(searchpaths, ';')

        return package.searchpath(name, searchpath)
    end
end

-- Compose a loader function from options.
--
-- @param search_fn function will be used to search a file from
--     path template
-- @param load_fn function will be used to load a file, found by
--     search function
-- @return function a loader, which first search for the file and
--     then loads it
local function gen_loader_func(search_fn, load_fn)
    assert(type(search_fn) == 'function', 'search_fn must be defined')
    assert(type(load_fn) == 'function', 'load_fn must be defined')

    return function(name)
        if not name then
            return "empty name of module"
        end
        local file, err = search_fn(name)
        if not file then
            return err
        end
        local loaded, err = load_fn(file, name)
        if err == nil then
            return loaded
        else
            return err
        end
    end
end

local search_lua = gen_search_func(searchroot, LUA_TEMPLATES)
local search_lib = gen_search_func(searchroot, LIB_TEMPLATES)
local search_rocks_lua = gen_search_func(searchroot, ROCKS_LUA_TEMPLATES, true)
local search_rocks_lib = gen_search_func(searchroot, ROCKS_LIB_TEMPLATES, true)

local search_funcs = {
    search_lua,
    search_lib,
    search_rocks_lua,
    search_rocks_lib,
    function(name) return package.searchpath(name, package.path) end,
    function(name) return package.searchpath(name, package.cpath) end,
}

local function search(name)
    if not name then
        return "empty name of module"
    end
    for _, searcher in ipairs(search_funcs) do
        local file = searcher(name)
        if file ~= nil then
            return file
        end
    end
    return nil
end

-- loader_preload 1
table.insert(package.loaders, 2, gen_loader_func(search_lua, load_lua))
table.insert(package.loaders, 3, gen_loader_func(search_lib, load_lib))
table.insert(package.loaders, 4, gen_loader_func(search_rocks_lua, load_lua))
table.insert(package.loaders, 5, gen_loader_func(search_rocks_lib, load_lib))
-- package.path   6
-- package.cpath  7
-- croot          8

rawset(package, "search", search)
rawset(package, "searchroot", searchroot)
rawset(package, "setsearchroot", setsearchroot)

-- Execute scripts or load modules pointed by TT_PRELOAD
-- environment variable.
local function run_preload()
    local tt_preload = os.getenv('TT_PRELOAD') or ''
    if #tt_preload == 0 then
        return
    end
    for _, script in ipairs(tt_preload:split(';')) do
        -- luacheck: ignore 542 empty if branch
        if #script == 0 then
            -- Ignore empty entries to allow duplicated semicolons
            -- and leading/trailing semicolons.
            --
            -- It simplifies construction of the environment
            -- variable value using concatenation.
        elseif script:endswith('.lua') then
            local fn = assert(loadfile(script))
            fn(script)
        else
            require(script)
        end
    end
end

return {
    uptime = uptime;
    pid = pid;
    compat = compat;
    _internal = {
        strip_cwd_from_path = strip_cwd_from_path,
        module_name_from_filename = module_name_from_filename,
        run_preload = run_preload,
    };
}
