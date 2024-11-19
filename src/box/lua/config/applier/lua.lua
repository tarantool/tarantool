local alloc = require('internal.alloc')
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

local function apply(config)
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

return {
    name = 'lua',
    apply = apply,
}
