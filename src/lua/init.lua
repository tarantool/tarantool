
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

local function search_cwd_lib(name)
    local path = "./?."..soext
    return package.searchpath(name, path)
end

local function search_cwd_lua(name)
    local path = "./?.lua;./?/init.lua"
    return package.searchpath(name, path)
end

local function traverse_rocks(name, pathes_search)
    local cwd = fio.cwd()
    local index = string.len(cwd) + 1
    local strerr = ""
    while index ~= nil do
        cwd = string.sub(cwd, 1, index - 1)
        for i, path in ipairs(pathes_search) do
            local file, err = package.searchpath(name, cwd .. path)
            if err == nil then
                return file
            end
            strerr = strerr .. err
        end
        index = string.find(cwd, "/[^/]*$")
    end
    return nil, strerr
end

local function search_rocks_lua(name)
    local pathes_search = {
        "/.rocks/share/tarantool/?.lua;",
        "/.rocks/share/tarantool/?/init.lua;",
    }
    return traverse_rocks(name, pathes_search)
end

local function search_rocks_lib(name)
    local pathes_search = {
        "/.rocks/lib/tarantool/?."..soext
    }
    return traverse_rocks(name, pathes_search)
end

local function cwd_loader_func(lib)
    local search_cwd = lib and search_cwd_lib or search_cwd_lua
    local load_func = lib and load_lib or load_lua
    return function(name)
        if not name then
            return "empty name of module"
        end
        local file, err = search_cwd(name)
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

local function rocks_loader_func(lib)
    local search_rocks = lib and search_rocks_lib or search_rocks_lua
    local load_func = lib and load_lib or load_lua
    return function (name)
        if not name then
            return "empty name of module"
        end
        local file, err = search_rocks(name)
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

local function search_path_func(cpath)
    return function(name)
        return package.searchpath(name, cpath and package.cpath or package.path)
    end
end

local function search(name)
    if not name then
        return "empty name of module"
    end
    local searchers = {
        search_cwd_lua, search_cwd_lib,
        search_rocks_lua, search_rocks_lib,
        search_path_func(false), search_path_func(true)
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
table.insert(package.loaders, 2, cwd_loader_func(false))
table.insert(package.loaders, 3, cwd_loader_func(true))
table.insert(package.loaders, 4, rocks_loader_func(false))
table.insert(package.loaders, 5, rocks_loader_func(true))
-- package.path   6
-- package.cpath  7
-- croot          8

package.search = search

return {
    uptime = uptime;
    pid = pid;
}
