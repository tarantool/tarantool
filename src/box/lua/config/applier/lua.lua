local alloc = require('internal.alloc')
local fio = require('fio')
local log = require('internal.config.utils.log')

-- After limiting the memory to some value we want to be sure
-- there is unused memory not to fail right after setting the
-- new limit.
--
-- This value represents the required amount of unused memory
-- after applying the lua.memory parameter.
--
-- The value is equal to 16MB. It has been chosen arbitrarily
-- as 1/16 of the minimum Lua memory limit.
local REQUIRED_UNUSED_MEMORY_AFTER_APPLICATION = 16 * 1024 * 1024

-- Point the module search root to process.work_dir.
--
-- The first box.cfg() call changes the current working directory
-- to process.work_dir and modules installed into it (for
-- example, into the .rocks subdirectory) become resolvable,
-- because the module search is relative to the current directory
-- by default.
--
-- However, some code runs before box.cfg(): roles and an
-- application marked with the early_load tag and their metadata
-- scan. Set the search root to make modules installed into the
-- working directory resolvable before the chdir occurs.
local function set_searchroot(config)
    if type(box.cfg) ~= 'function' then
        -- box.cfg() is already called, the current working
        -- directory is process.work_dir. Nothing to do.
        return
    end
    local configdata = config._configdata
    local work_dir = configdata:get('process.work_dir', {use_default = true})
    if work_dir == nil then
        return
    end
    -- A relative process.work_dir is interpreted against the
    -- startup working directory, which is not changed yet.
    work_dir = fio.abspath(work_dir)
    package.setsearchroot(work_dir)
    log.verbose('lua.apply: set the module search root to %q', work_dir)
end

local function set_memory_limit(config)
    local configdata = config._configdata
    local memory_limit = configdata:get('lua.memory', {use_default = true})
    local old_memory_limit = alloc.getlimit()
    local used_memory = alloc.used()

    -- Nothing to do if the limit is unchanged.
    if memory_limit == old_memory_limit then
        return
    end

    -- Check there is enough unused space after applying
    -- the new Lua memory limit.
    --
    -- Otherwise, the alert is set.
    if memory_limit <
       used_memory + REQUIRED_UNUSED_MEMORY_AFTER_APPLICATION then
        local name = 'lua_memory_limit_too_small'
        local warning = 'lua.apply: lua.memory will be applied ' ..
                        'after restarting the instance since the ' ..
                        'new limit is too close to the currently ' ..
                        'allocated amount of memory'
        config._aboard:set({type = 'warn', message = warning}, {key = name})
        return
    end

    -- The call throws an error if the user tries to limit the
    -- memory with values < 256MB.
    alloc.setlimit(memory_limit)

    log.verbose(('lua.apply: set memory limit to %d'):format(memory_limit))
end

local function apply(config)
    set_searchroot(config)
    set_memory_limit(config)
end

return {
    name = 'lua',
    apply = apply,
}
