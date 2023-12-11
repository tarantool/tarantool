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

-- Tarantool's builtin modules.
--
-- Similar to _LOADED (package.loaded).
local builtin_modules = debug.getregistry()._TARANTOOL_BUILTIN

-- Loader for built-in modules.
local function builtin_loader(name)
    -- A loader is more like a searching function rather than a
    -- loading function (fun fact: package.loaders was renamed to
    -- package.searchers in Lua 5.2).
    --
    -- A loader (a searcher) typically searches for a file and, if
    -- the file is found, loads it and returns a function to
    -- execute it. Typically just result of loadfile(file).
    --
    -- Our 'filesystem' is a table of modules, a 'file' is an
    -- entry in the table. If a module is found, the loader
    -- returns a function that 'executes' it. The function just
    -- returns the module itself.
    if builtin_modules[name] ~= nil then
        return function(_name)
            return builtin_modules[name]
        end
    end

    return ("\n\tno field loaders.builtin['%s']"):format(name)
end

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
        local message = ("error loading module '%s' from file '%s':\n\t%s")
                        :format(name, file, err)
        return assert(loaded, message)
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

-- Accept a loader and return a loader, which search by a prefixed
-- module name.
--
-- The module receives the original (unprefixed) module name in
-- the argument (three dots).
local function prefix_loader(prefix, subloader)
    return function(name)
        local prefixed_name = prefix .. '.' .. name
        -- On success the return value is a function, which
        -- executes module's initialization code. require() calls
        -- it with one argument: the module name (it can be
        -- received in the module using three dots). Since
        -- require() knows nothing about our prefixing it passes
        -- the original name there.
        --
        -- It is expected behavior in our case. The prefixed
        -- loaders are added to enable extra search paths: like
        -- we would add more package.{path,cpath} entries. It
        -- shouldn't change the string passed to the module's
        -- initialization code.
        return subloader(prefixed_name)
    end
end

-- Accept a loader and return the same loader, but enabled only
-- when given condition (a function return value) is true.
local function conditional_loader(subloader, onoff)
    assert(type(onoff) == 'function')
    return function(name)
        if onoff(name) then
            return subloader(name)
        end
        -- It is okay to return nothing, require() ignores it.
    end
end

-- Accept an array of loaders and return a loader, whose effect is
-- equivalent to calling the loaders in a row.
local function chain_loaders(subloaders)
    return function(name)
        -- Error accumulator.
        local err = ''

        for _, loader in ipairs(subloaders) do
            local loaded = loader(name)
            -- Whether the module found? Let's return it.
            --
            -- loaded is a function, which executes module's
            -- initialization code.
            if type(loaded) == 'function' then
                return loaded
            end
            -- If the module is not found and the loader function
            -- returns an error, add the error into the
            -- accumulator.
            if type(loaded) == 'string' then
                err = err .. loaded
            end
            -- Ignore any other return value: require() does the
            -- same.
        end

        return err
    end
end

-- loader_preload 1
table.insert(package.loaders, 2, gen_loader_func(search_lua, load_lua))
table.insert(package.loaders, 3, gen_loader_func(search_lib, load_lib))
table.insert(package.loaders, 4, gen_loader_func(search_rocks_lua, load_lua))
table.insert(package.loaders, 5, gen_loader_func(search_rocks_lib, load_lib))
-- package.path   6
-- package.cpath  7
-- croot          8

-- Search for modules next to the main script.
--
-- If the script is not provided (script == nil) or provided as
-- stdin (script == '-'), there is no script directory, so nothing
-- to do here.
local script = minifio.script()
if script ~= nil and script ~= '-' then
    -- It is important to obtain the directory at initialization,
    -- before any cwd change may occur. The script path may be
    -- passed as relative to the current directory.
    local script_dir = minifio.dirname(minifio.abspath(script))
    local function script_dir_fn()
        return script_dir
    end

    -- Search non-recursively, only next to the script.
    local search_app_lua = gen_search_func(script_dir_fn, LUA_TEMPLATES)
    local search_app_lib = gen_search_func(script_dir_fn, LIB_TEMPLATES)
    local search_app_rocks_lua = gen_search_func(script_dir_fn,
        ROCKS_LUA_TEMPLATES)
    local search_app_rocks_lib = gen_search_func(script_dir_fn,
        ROCKS_LIB_TEMPLATES)

    -- Mix the script directory loaders into corresponding
    -- searchroot based loaders. It allows to avoid changing
    -- ordinals of the loaders and also makes the override
    -- loader search here.
    --
    -- We can just add more paths to package.path/package.cpath,
    -- but:
    --
    -- * Search for override modules is implemented as a loader.
    -- * Search inside .rocks in implemented as a loaders.
    --
    -- And it is simpler to wrap this logic rather than repeat.
    -- It is possible (and maybe even desirable) to reimplement
    -- all the loaders logic as paths generation, but we should
    -- do that for all the logic at once.
    package.loaders[2] = chain_loaders({
        package.loaders[2],
        gen_loader_func(search_app_lua, load_lua),
    })
    package.loaders[3] = chain_loaders({
        package.loaders[3],
        gen_loader_func(search_app_lib, load_lib),
    })
    package.loaders[4] = chain_loaders({
        package.loaders[4],
        gen_loader_func(search_app_rocks_lua, load_lua),
    })
    package.loaders[5] = chain_loaders({
        package.loaders[5],
        gen_loader_func(search_app_rocks_lib, load_lib),
    })
end

local function getenv_boolean(varname, default)
    local envvar = os.getenv(varname)

    -- If unset or empty, use the default.
    if envvar == nil or envvar == '' then
        return default
    end

    -- Explicitly enabled or disabled.
    --
    -- Accept false/true case insensitively.
    --
    -- Accept 0/1 as boolean values.
    if envvar:lower() == 'false' or envvar == '0' then
        return false
    end
    if envvar:lower() == 'true' or envvar == '1' then
        return true
    end

    -- Can't parse the value, let's use the default.
    return default
end

-- true/false if explicitly enabled or disabled, nil otherwise.
local override_loader_is_enabled

-- Whether the override loader is enabled.
local function override_loader_onoff(_name)
    -- Follow the switch if it is explicitly enabled or disabled.
    if override_loader_is_enabled ~= nil then
        return override_loader_is_enabled
    end

    -- Follow the environment variable otherwise.
    return getenv_boolean('TT_OVERRIDE_BUILTIN', true)
end

local override_loader = conditional_loader(chain_loaders({
    prefix_loader('override', package.loaders[2]),
    prefix_loader('override', package.loaders[3]),
    prefix_loader('override', package.loaders[4]),
    prefix_loader('override', package.loaders[5]),
    prefix_loader('override', package.loaders[6]),
    prefix_loader('override', package.loaders[7]),
}), override_loader_onoff)

-- Add two loaders:
--
-- - Search for override.<module_name> module. It is necessary for
--   overriding built-in modules.
-- - Search for a built-in module (compiled into tarantool's
--   executable).
--
-- Those two loaders are mixed into the first loader to don't
-- change ordinals of the loaders 2-8. It is possible that someone
-- has a logic based on those loader positions.
package.loaders[1] = chain_loaders({
    package.loaders[1],
    override_loader,
    builtin_loader,
})

rawset(package, "search", search)
rawset(package, "searchroot", searchroot)
rawset(package, "setsearchroot", setsearchroot)

local no_package_loaded = {}
local raw_require = require

-- Allow an override module to refuse caching in package.loaded.
--
-- It may be useful to return a built-in module into the platform,
-- but the override module into application's code.
_G.require = function(modname)
    if no_package_loaded[modname] then
        no_package_loaded[modname] = nil
        package.loaded[modname] = nil
    end
    -- NB: This call is a tail call and it is important to some
    -- extent. At least luajit's test suites expect certain error
    -- messages from `require()` calls and from the `-l` option.
    --
    -- A non-tail call changes the filename at the beginning of
    -- the error message.
    return raw_require(modname)
end

return {
    ROCKS_LIB_PATH = ROCKS_LIB_PATH,
    ROCKS_LUA_PATH = ROCKS_LUA_PATH,
    builtin = builtin_modules,
    override_builtin_enable = function()
        override_loader_is_enabled = true
    end,
    override_builtin_disable = function()
        override_loader_is_enabled = false
    end,
    -- It is `true` during tarantool initialization, but once all
    -- the built-in modules are ready, will be set to `nil`.
    initializing = true,
    raw_require = raw_require,
    -- Add a module name here to ignore package.loaded[modname] at
    -- next require(). The flag is dropped at the require() call.
    --
    -- This is intended to be used from inside a module to refuse
    -- caching in package.loaded. Any other usage has unspecified
    -- behavior (just manipulate package.loaded directly if you
    -- need it).
    --
    -- Usage: loaders.no_package_loaded[modname] = true
    no_package_loaded = no_package_loaded,
}
