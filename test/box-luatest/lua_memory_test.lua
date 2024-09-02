local server = require('luatest.server')
local t = require('luatest')
local g = t.group('lua_memory setting test')

local MB = 1024 * 1024
local LUA_MIN_MEMORY = 256 * MB

g.before_each(function()
    g.server = server:new({
        alias = 'default',
        box_cfg = {lua_memory = LUA_MIN_MEMORY}
    })
    g.server:start()

end)

g.after_each(function()
    g.server:drop()
end)

g.test_lua_memory_setting = function(g)
    g.server:exec(function()
        local MB = 1024 * 1024
        local LUA_MIN_MEMORY = 256 * MB
        local LUA_EXTENDED_MEMORY = 600 * MB

        t.assert_equals(box.cfg.lua_memory, LUA_MIN_MEMORY)

        -- Increasing the limit should be ok.
        box.cfg{lua_memory = LUA_MIN_MEMORY + 1}
        t.assert_equals(box.cfg.lua_memory, LUA_MIN_MEMORY + 1)

        local err_min = 'Cannot limit the Lua memory with values less than ' ..
                        '256MB'
        -- Setting the limit less than 256MB is prohibited.
        t.assert_error_msg_equals(err_min, box.cfg,
            {lua_memory = LUA_MIN_MEMORY - 1})

        box.cfg{lua_memory = LUA_EXTENDED_MEMORY}

        local used = collectgarbage('count') * 1024
        local free = LUA_EXTENDED_MEMORY - used

        -- This allocation should be successful.
        local _ = string.rep('a', free / 3)

        local err_decreasing = 'Cannot limit the Lua memory with values ' ..
                               'less than the currently allocated amount'
        -- Decreasing less than currently allocated is forbidden.
        t.assert_error_msg_equals(err_decreasing, box.cfg,
            {lua_memory = LUA_MIN_MEMORY})

    end)

end

g.test_lua_memory_semantics = function()
    g.server:exec(function()
        local memory = 256 * 1024 * 1024

        local function overflow()
            local free = memory - (collectgarbage('count')) * 1024
            local _ = string.rep('a', free / 2)
        end

        t.assert_error_msg_equals('not enough memory', overflow)
        t.assert_str_contains(tostring(box.error.last()), 'Failed to allocate')
    end)
end
