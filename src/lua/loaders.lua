local minifio = require('internal.minifio')

local soext = (jit.os == "OSX" and "dylib" or "so")

local ROCKS_LIB_PATH = '.rocks/lib/tarantool'
local ROCKS_LUA_PATH = '.rocks/share/tarantool'
local LIB_TEMPLATES = { '?.'..soext }
local LUA_TEMPLATES = { '?.lua', '?/init.lua' }
local ROCKS_LIB_TEMPLATES = {
    ROCKS_LIB_PATH .. '/?.'..soext,
}
local ROCKS_LUA_TEMPLATES = {
    ROCKS_LUA_PATH .. '/?.lua',
    ROCKS_LUA_PATH .. '/?/init.lua',
}

local package_searchroot

local function searchroot()
    return package_searchroot or minifio.cwd()
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
    package_searchroot = path and minifio.abspath(path)
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
    path = minifio.abspath(path)
    local paths = { path }

    while path ~= '/' do
        path = minifio.dirname(path)
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
                table.insert(searchpaths, minifio.pathjoin(path, template))
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

return {
    ROCKS_LIB_PATH = ROCKS_LIB_PATH,
    ROCKS_LUA_PATH = ROCKS_LUA_PATH,
}
