
-- init.lua -- internal file

local ffi = require('ffi')
local loaders = require('internal.loaders')

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
    result = result:gsub(loaders.ROCKS_LIB_PATH .. '/', '')
    result = result:gsub(loaders.ROCKS_LUA_PATH .. '/', '')
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

-- Extract all fields from a table except ones that start from
-- the underscore.
--
-- Useful for __serialize.
local function filter_out_private_fields(t)
    local res = {}
    for k, v in pairs(t) do
        if not k:startswith('_') then
            res[k] = v
        end
    end
    return res
end

local mt = {
    __serialize = filter_out_private_fields,
}

return setmetatable({
    uptime = uptime,
    pid = pid,
    _internal = {
        strip_cwd_from_path = strip_cwd_from_path,
        module_name_from_filename = module_name_from_filename,
        run_preload = run_preload,
    },
}, mt)
