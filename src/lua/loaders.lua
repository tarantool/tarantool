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

-- Generate a path builder function, which yields the set of the
-- paths to be searched through for the desired module.
--
-- @param basepath_fn function which returns a base path for the
--     resulting path set
-- @param templates table with lua search templates
-- @param need_traverse bool flag which tells search function to
--     build multiple paths by expanding base path up to the
--     root ('/')
-- @return a path builder function which builds and yields the
--     resulting path set
local function gen_path_builder(basepath_fn, templates, need_traverse)
    assert(type(basepath_fn) == 'function', 'basepath_fn must be a function')
    assert(type(templates) == 'table', 'templates must be a table')

    return function()
        local base = basepath_fn() or '.'
        local dirs = need_traverse and traverse_path(base) or { base }

        local searchpaths = {}

        for _, dir in ipairs(dirs) do
            for _, template in pairs(templates) do
                table.insert(searchpaths, minifio.pathjoin(dir, template))
            end
        end

        return table.concat(searchpaths, ';')
    end
end

local path = {
    package = function() return package.path end,
    cwd = {
        dot = gen_path_builder(searchroot, LUA_TEMPLATES),
        rocks = gen_path_builder(searchroot, ROCKS_LUA_TEMPLATES, true),
    }
}

local cpath = {
    package = function() return package.cpath end,
    cwd = {
        dot = gen_path_builder(searchroot, LIB_TEMPLATES),
        rocks = gen_path_builder(searchroot, ROCKS_LIB_TEMPLATES, true),
    }
}

local function gen_file_searcher(loader, pathogen)
    assert(type(loader) == 'function', '<loader> must be a function')
    assert(type(pathogen) == 'function', '<pathogen> must be a function')

    return function(name)
        local path, errmsg = package.searchpath(name, pathogen())
        if not path then
            return errmsg
        end
        return loader, path
    end
end

-- Lua table with "package searchers".
local searchers = debug.getregistry()._TARANTOOL_PACKAGE_SEARCHERS

rawset(searchers, 'preload', function(name)
    assert(type(package.preload) == 'table',
           "'package.preload' must be a table")
    local loader = package.preload[name]
    if loader ~= nil then
        return loader, ':preload:'
    end
    return ("\n\tno field package.preload['%s']"):format(name)
end)

-- Tarantool's builtin modules.
--
-- Similar to _LOADED (package.loaded).
local builtin_modules = debug.getregistry()._TARANTOOL_BUILTIN

-- Searcher for builtin modules.
rawset(searchers, 'builtin', function(name)
    local module = builtin_modules[name]
    if module ~= nil then
        return function(_name) return module end, ':builtin:'
    end
    return ("\n\tno field loaders.builtin['%s']"):format(name)
end)

rawset(searchers, 'path.package', gen_file_searcher(load_lua, path.package))
rawset(searchers, 'cpath.package', gen_file_searcher(load_lib, cpath.package))
rawset(searchers, 'path.cwd.dot', gen_file_searcher(load_lua, path.cwd.dot))
rawset(searchers, 'cpath.cwd.dot', gen_file_searcher(load_lib, cpath.cwd.dot))
rawset(searchers, 'path.cwd.rocks', gen_file_searcher(load_lua, path.cwd.rocks))
rawset(searchers, 'cpath.cwd.rocks', gen_file_searcher(load_lib, cpath.cwd.rocks))

-- Indices in <searchers> table must represent the order of the
-- functions in <package.loaders> below.
rawset(searchers, 2, searchers['path.cwd.dot'])
rawset(searchers, 3, searchers['cpath.cwd.dot'])
rawset(searchers, 4, searchers['path.cwd.rocks'])
rawset(searchers, 5, searchers['cpath.cwd.rocks'])
rawset(searchers, 6, searchers['path.package'])
rawset(searchers, 7, searchers['cpath.package'])

-- Compose a loader function from options.
--
-- @param searcher function will be used to search a file from
--     path template
-- @return function a loader, which first search for the file and
--     then loads it
local function gen_file_loader(searcher)
    assert(type(searcher) == 'function', '<searcher> must be defined')

    return function(name)
        if not name then
            return "empty name of module"
        end
        local loader, data = searcher(name)
        -- XXX: <loader> (i.e. the first return value) contains
        -- error message in this case. Just propagate it to the
        -- <require> frame...
        if not data then
           return loader
        end
        -- XXX: ... Otherwise, this is a valid module loader.
        -- Load the given <data> and return the result.
        local loaded, err = loader(data, name)
        local message = ("error loading module '%s' from file '%s':\n\t%s")
                        :format(name, data, err)
        return assert(loaded, message)
    end
end

-- XXX: Though <package.search> looks like a handy public
-- function, it's only the helper used for searching Tarantool
-- C modules. Hence, despite it uses the same searchers as loaders
-- for Lua modules do, it follows its own semantics ignoring
-- preload, override, builtin modules, and application specific
-- locations.
-- FIXME: Lua paths look really odd in the list, since the
-- function is used to load only C modules. However, these paths
-- are left to avoid breaking change.
local search_funcs = {
    searchers['path.cwd.dot'],
    searchers['cpath.cwd.dot'],
    searchers['path.cwd.rocks'],
    searchers['cpath.cwd.rocks'],
    searchers['path.package'],
    searchers['cpath.package'],
}

local function search(name)
    if not name then
        return "empty name of module"
    end
    for _, searcher in ipairs(search_funcs) do
        local loader, file = searcher(name)
        if type(loader) == 'function' then
            return file
        end
    end
    return nil
end

-- Accept a searcher and return a searcher, which search by a
-- prefixed module name.
--
-- The module receives the original (unprefixed) module name in
-- the argument (three dots).
local function prefix_searcher(prefix, subsearcher)
    return function(name)
        local prefixed_name = prefix .. '.' .. name
        -- On success the return value is a function, which
        -- implements loading mechanism for particular file.
        -- If the file is loaded successfully, the loader yields
        -- module initialization function that is called by
        -- <require> with one argument: the module name (it can be
        -- received in the module using three dots). Since
        -- <require> knows nothing about our prefixing, it passes
        -- the original name there.
        --
        -- It is expected behavior in our case. The prefixed
        -- loaders are added to enable extra search paths: like
        -- we would add more package.{path,cpath} entries. It
        -- shouldn't change the string passed to the module's
        -- initialization code.
        return subsearcher(prefixed_name)
    end
end

-- Accept a searcher and return the same searcher, but enabled
-- only when given condition (a function return value) is true.
local function conditional_searcher(subsearcher, onoff)
    assert(type(onoff) == 'function')
    return function(name)
        if onoff(name) then
            return subsearcher(name)
        end
        -- It is okay to return nothing, <require> ignores it.
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

-- Accept an array of searchers and return a searcher, whose
-- effect is equivalent to calling the searchers in a row.
local function chain_searchers(subsearchers)
    return function(name)
        -- Error accumulator.
        local err = ''

        for _, searcher in ipairs(subsearchers) do
            local loader, data = searcher(name)
            -- Whether the module found? Let's return it.
            --
            -- <loader> is a function, which implements loading
            -- routine for particular <data> module.
            if type(loader) == 'function' then
                return loader, data
            end
            -- If the module is not found and the searcher
            -- function returns an error, add the error into the
            -- accumulator.
            if type(loader) == 'string' then
                err = err .. loader
            end
            -- Ignore any other return value: require() does the
            -- same.
        end

        return err
    end
end


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
    path.app = {
        dot = gen_path_builder(script_dir_fn, LUA_TEMPLATES),
        rocks = gen_path_builder(script_dir_fn, ROCKS_LUA_TEMPLATES),
    }
    cpath.app = {
        dot = gen_path_builder(script_dir_fn, LIB_TEMPLATES),
        rocks = gen_path_builder(script_dir_fn, ROCKS_LIB_TEMPLATES)
    }

    rawset(searchers, 'path.app.dot', gen_file_searcher(load_lua, path.app.dot))
    rawset(searchers, 'cpath.app.dot', gen_file_searcher(load_lib, cpath.app.dot))
    rawset(searchers, 'path.app.rocks', gen_file_searcher(load_lua, path.app.rocks))
    rawset(searchers, 'cpath.app.rocks', gen_file_searcher(load_lib, cpath.app.rocks))

    -- Mix the script directory searchers into corresponding
    -- searchroot based searchers. It allows to avoid changing
    -- ordinals of the searchers (and hence package.loaders) and
    -- also makes the override loader search here.
    --
    -- We can just add more paths to package.path/package.cpath,
    -- but:
    --
    -- * Search for override modules is implemented as a loader.
    -- * Search inside .rocks directory is implemented as a
    --   separate searchers.
    --
    -- And it is simpler to wrap this logic rather than repeat.
    -- It is possible (and maybe even desirable) to reimplement
    -- all the searchers logic as paths generation, but we should
    -- do that for all the logic at once.
    rawset(searchers, 2, chain_searchers({
        searchers[2],
        searchers['path.app.dot'],
    }))
    rawset(searchers, 3, chain_searchers({
        searchers[3],
        searchers['cpath.app.dot'],
    }))
    rawset(searchers, 4, chain_searchers({
        searchers[4],
        searchers['path.app.rocks'],
    }))
    rawset(searchers, 5, chain_searchers({
        searchers[5],
        searchers['cpath.app.rocks'],
    }))
end

-- loader_preload 1
table.insert(package.loaders, 2, gen_file_loader(searchers[2]))
table.insert(package.loaders, 3, gen_file_loader(searchers[3]))
table.insert(package.loaders, 4, gen_file_loader(searchers[4]))
table.insert(package.loaders, 5, gen_file_loader(searchers[5]))
-- package.path   6
-- package.cpath  7
-- croot          8

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
local override_searcher_is_enabled

-- Whether the override loader is enabled.
local function override_searcher_onoff(_name)
    -- Follow the switch if it is explicitly enabled or disabled.
    if override_searcher_is_enabled ~= nil then
        return override_searcher_is_enabled
    end

    -- Follow the environment variable otherwise.
    return getenv_boolean('TT_OVERRIDE_BUILTIN', true)
end

local override_loader = gen_file_loader(conditional_searcher(chain_searchers({
    prefix_searcher('override', searchers[2]),
    prefix_searcher('override', searchers[3]),
    prefix_searcher('override', searchers[4]),
    prefix_searcher('override', searchers[5]),
    prefix_searcher('override', searchers[6]),
    prefix_searcher('override', searchers[7]),
}), override_searcher_onoff))


local function dummy_loader(searcher, sentinel)
    return function(name)
        local loader, data = searcher(name)
        -- XXX: <loader> (i.e. the first return value) contains
        -- error message in this case. Just propagate it to the
        -- <require> frame...
        if not data then
           return loader
        end
        -- XXX: ... Otherwise, this is a valid module loader.
        -- Check the <data> and return the <loader> function.
        assert(data == sentinel, 'Invalid searcher')
        return loader
    end
end

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
    dummy_loader(searchers['preload'], ':preload:'),
    override_loader,
    dummy_loader(searchers['builtin'], ':builtin:'),
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
        override_searcher_is_enabled = true
    end,
    override_builtin_disable = function()
        override_searcher_is_enabled = false
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
