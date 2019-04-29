
-- init.lua -- internal file

local ffi = require('ffi')
ffi.cdef[[
struct type_info;
struct method_info;

enum ctype {
    CTYPE_VOID = 0,
    CTYPE_INT,
    CTYPE_CONST_CHAR_PTR
};

struct type_info {
    const char *name;
    const struct type_info *parent;
    const struct method_info *methods;
};

enum { METHOD_ARG_MAX = 8 };

struct method_info {
    const struct type_info *owner;
    const char *name;
    enum ctype rtype;
    enum ctype atype[METHOD_ARG_MAX];
    int nargs;
    bool isconst;

    union {
        /* Add extra space to get proper struct size in C */
        void *_spacer[2];
    };
};

double
tarantool_uptime(void);
typedef int32_t pid_t;
pid_t getpid(void);
void
tarantool_exit(int);
]]

local fio = require("fio")

local function get_scriptpath()
    local scriptpath = debug.getinfo(2, "S").source:sub(2):match("(.*/)") or './'
    return fio.abspath(scriptpath)
end

local package_appdir

local function set_appdir(dir)
    assert(type(dir) == 'string', 'Application directory must be a string')

    package_appdir = fio.abspath(dir)
end

local function get_appdir()
    return package_appdir or fio.cwd()
end

local ROCKS_LIB_PATH = '.rocks/lib/tarantool'
local ROCKS_LUA_PATH = '.rocks/share/tarantool'

local function get_rockslibdir()
    return fio.pathjoin(get_appdir(), ROCKS_LIB_PATH)
end

local function get_rocksluadir()
    return fio.pathjoin(get_appdir(), ROCKS_LUA_PATH)
end

dostring = function(s, ...)
    local chunk, message = loadstring(s)
    if chunk == nil then
        error(message, 2)
    end
    return chunk(...)
end

local fiber = require("fiber")
os.exit = function(code)
    code = (type(code) == 'number') and code or 0
    ffi.C.tarantool_exit(code)
    -- Make sure we yield even if the code after
    -- os.exit() never yields. After on_shutdown
    -- fiber completes, we will never wake up again.
    while true do fiber.yield() end
end

local function uptime()
    return tonumber(ffi.C.tarantool_uptime());
end

local function pid()
    return tonumber(ffi.C.getpid())
end

local soext = (jit.os == "OSX" and "dylib" or "so")

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

local function merge_strings(paths, templates)
    local searchpaths = {}
    paths = type(paths) == 'table' and paths or { paths }
    templates = templates or {}

    if #templates == 0 then
        return paths
    end

    if #paths == 0 then
        return templates
    end

    for _, path in ipairs(paths) do
        for _, template in pairs(templates) do
            table.insert(searchpaths, fio.pathjoin(path, template))
        end
    end

    return searchpaths
end

local LIB_TEMPLATES = { '?.'..soext }
local LUA_TEMPLATES = { '?.lua', '?/init.lua' }
local ROCKS_LIB_TEMPLATES = { ROCKS_LIB_PATH .. '/?.'..soext }
local ROCKS_LUA_TEMPLATES = { ROCKS_LUA_PATH .. '/?.lua', ROCKS_LUA_PATH .. '/?/init.lua' }

local function search_lib(name, path)
    return package.searchpath(name, table.concat(merge_strings(path, LIB_TEMPLATES), ';'))
end

local function search_lua(name, path)
    return package.searchpath(name, table.concat(merge_strings(path, LUA_TEMPLATES), ';'))
end

local function split_path(path)
    path = path:sub(-1) == '/' and path:sub(1, -2) or path
    local index = string.len(path) + 1
    local paths = {}

    while index ~= nil do
        path = string.sub(path, 1, index - 1)
        table.insert(paths, path .. '/')
        index = string.find(path, "/[^/]*$")
    end

    return paths
end

local function get_search_func(opts)
    local path_fn = opts.path_fn
    if type(opts.path_fn) ~= 'function' then
        path_fn = function() return opts.path_fn end
    end

    return function(name)
        local path = path_fn() or '.'
        local paths = opts.traverse and split_path(path) or { path }
        local searchpath = table.concat(merge_strings(paths, opts.templates), ';')
        return package.searchpath(name, searchpath)
    end
end

local function get_loader_func(opts)
    opts = opts or {}
    opts.path_fn = opts.path_fn or function() return fio.cwd end
    opts.is_lib = not (not opts.is_lib)
    opts.traverse = not (not opts.traverse)

    local load_func = opts.is_lib and load_lib or load_lua
    opts.templates = opts.is_lib and LIB_TEMPLATES or LUA_TEMPLATES
    opts.is_lib = nil
    local search_func = get_search_func(opts)

    return function(name)
        if not name then
            return "empty name of module"
        end
        local file, err = search_func(name)
        if not file then
            return err
        end
        local loaded, err = load_func(file, name)
        if err == nil then
            return loaded
        else
            return err
        end
    end
end

local function search(name)
    if not name then
        return "empty name of module"
    end
    local searchers = {
        get_search_func({path_fn = fio.cwd, templates = LUA_TEMPLATES}),
        get_search_func({path_fn = fio.cwd, templates = LIB_TEMPLATES}),
        get_search_func({path_fn = fio.cwd, templates = ROCKS_LUA_TEMPLATES, traverse = true}),
        get_search_func({path_fn = fio.cwd, templates = ROCKS_LIB_TEMPLATES, traverse = true}),
        get_search_func({path_fn = function() return package.path end}),
        get_search_func({path_fn = function() return package.cpath end}),
    }
    for _, searcher in ipairs(searchers) do
        local file = searcher(name)
        if file ~= nil then
            return file
        end
    end
    return nil
end

-- loader_preload 1
table.insert(package.loaders, 2, get_loader_func({path_fn = fio.cwd, is_lib = false}))
table.insert(package.loaders, 3, get_loader_func({path_fn = fio.cwd, is_lib = true}))
table.insert(package.loaders, 4, get_loader_func({path_fn = get_appdir, is_lib = false}))
table.insert(package.loaders, 5, get_loader_func({path_fn = get_appdir, is_lib = true}))
table.insert(package.loaders, 6, get_loader_func({path_fn = get_rocksluadir, is_lib = false, traverse = true}))
table.insert(package.loaders, 7, get_loader_func({path_fn = get_rockslibdir, is_lib = true, traverse = true}))
-- package.path   8
-- package.cpath  9
-- croot         10

package.search = search
package.set_appdir = set_appdir
package.get_appdir = get_appdir
package.get_scriptpath = get_scriptpath

return {
    uptime = uptime;
    pid = pid;
}
