local server = require('luatest.server')
local t = require('luatest')

local g_recovery = t.group('recovery')

g_recovery.after_all(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
        cg.server = nil
    end
end)

g_recovery.test_recovery = function(cg)
    local add_triggers = [[
        local trigger = require('trigger')
        trigger.set('box.on_commit.space._space', 'check', function() end)
    ]]
    cg.server = server:new({
        env = {TARANTOOL_RUN_BEFORE_BOX_CFG = add_triggers}
    })
    cg.server:start()
end

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
        cg.server = nil
    end
end)

g.test_on_commit = function(cg)
    cg.server:exec(function()
        local trigger = require('trigger')

        trigger.set('box.on_commit.space._space', 'check', function() end)

        local s = box.space._space:get(box.space._space.id):totable()
        -- Change field_count field (currently is 0 for _space).
        s[5] = #box.space._space:format()
        box.space._space:replace(s)
    end)
end
