local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

local setup_triggers = [[
    local compat = require('compat')
    local trigger = require('trigger')

    local id = %d
    local name = '%s'

    compat.box_recovery_triggers_deprecation = 'new'

    local events = {
        'box.space[' .. id .. '].before_recovery_replace',
        'box.space.' .. name .. '.before_recovery_replace',
        'box.space[' .. id .. '].on_recovery_replace',
        'box.space.' .. name .. '.on_recovery_replace',
        'box.before_commit',
        'box.before_commit.space.' .. name,
        'box.before_commit.space[' .. id .. ']',
        'box.on_commit',
        'box.on_commit.space.' .. name,
        'box.on_commit.space[' .. id .. ']',
    }

    for _, event in ipairs(events) do
        trigger.set(
            event, 'event_check', function()
               -- luatest server gives grants for guest, so check
               -- current box status to filter out correspondent
               -- trigger invocation.
                if box.info.status ~= "running" and
                   rawget(_G, 'trigger_fired') == nil then
                    rawset(_G, 'trigger_fired', event)
                end
            end
        )
    end
]]

g.after_all(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
        cg.server = nil
    end
    if cg.replica ~= nil then
        cg.replica:drop()
        cg.replica = nil
    end
end)

g.test_recovery_triggers = function(cg)
    -- Bootstrap test.
    local before_box_cfg = string.format(setup_triggers, 280, '_space')
    cg.server = server:new({
        env = {TARANTOOL_RUN_BEFORE_BOX_CFG = before_box_cfg}
    })
    cg.server:start()
    cg.server:exec(function()
        local compat = require('compat')

        t.assert_equals(compat.box_recovery_triggers_deprecation.current, 'new')
        t.assert_equals(rawget(_G, 'trigger_fired'), nil)

        local s = box.schema.create_space('test')
        s:create_index('pk')
        s:insert({1})
        s:insert({2})

        t.assert_equals(s.id, 512)
    end)

    -- Local recovery test.
    local before_box_cfg = string.format(setup_triggers, 512, 'test')
    cg.server:restart({
        env = {TARANTOOL_RUN_BEFORE_BOX_CFG = before_box_cfg}
    })
    cg.server:exec(function()
        local compat = require('compat')

        t.assert_equals(compat.box_recovery_triggers_deprecation.current, 'new')
        t.assert_equals(rawget(_G, 'trigger_fired'), nil)
    end)

    -- Replica join test.
    cg.replica = server:new({
        alias = 'replica',
        env = {TARANTOOL_RUN_BEFORE_BOX_CFG = before_box_cfg},
        box_cfg = {replication = cg.server.net_box_uri },
    })
    cg.replica:start()
    cg.replica:wait_for_vclock_of(cg.server)

    cg.replica:exec(function()
        local compat = require('compat')

        t.assert_equals(compat.box_recovery_triggers_deprecation.current, 'new')
        t.assert_equals(rawget(_G, 'trigger_fired'), nil)
    end)
end
