local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function()
    g.server = server:new{
        alias = 'default',
    }
    g.server:start()
end)

g.after_each(function()
    g.server:drop()
    g.server = nil
end)

g.test_recovery = function()
    g.server:exec(function()
        local trigger = require('trigger')
        local max_i = 5
        local event_name = 'persistent_event'
        trigger.set(event_name, 'not_truly_persistent' .. tostring(max_i + 1),
                    function() end)
        for i = max_i, 1, -1 do
            box.schema.func.create('truly_persistent' .. tostring(i), {
                language = 'lua', body = 'function() end', trigger = event_name
            })
            trigger.set(event_name, 'not_truly_persistent' .. tostring(i),
                        function() end)
        end
        local triggers = trigger.info(event_name)[event_name]
        t.assert_equals(#triggers, max_i * 2 + 1)
        for i = 1, max_i do
            t.assert_equals(triggers[i * 2 - 1][1],
                            'not_truly_persistent' .. tostring(i))
            t.assert_equals(triggers[i * 2][1],
                            'truly_persistent' .. tostring(i))
        end
        t.assert_equals(triggers[max_i * 2 + 1][1],
                        'not_truly_persistent' .. tostring(max_i + 1))
    end)

    -- Check that triggers are truly persistent and the order is saved.
    g.server:restart()
    g.server:exec(function()
        local trigger = require('trigger')
        local max_i = 5
        local event_name = 'persistent_event'
        local triggers = trigger.info(event_name)[event_name]
        t.assert_equals(#triggers, max_i)
        for i = 1, max_i do
            t.assert_equals(triggers[i][1], 'truly_persistent' .. tostring(i))
        end
    end)
end

g.test_transactional = function()
    g.server:exec(function()
        local trigger = require('trigger')
        local event_name = 'tx_event'
        local trigger_name = 'test.rollback'
        local origin_trigger = {"I am an origin trigger!"}
        local mt = {}
        mt.__call = function() end
        setmetatable(origin_trigger, mt)

        -- Check that rollback deletes trigger.
        t.assert_equals(trigger.info(event_name), {})
        box.begin()
        box.schema.func.create(trigger_name,
            {language = 'lua', trigger = event_name})
        t.assert_equals(#trigger.info(event_name)[event_name], 1)
        box.rollback()
        t.assert_equals(trigger.info(event_name), {})

        -- Check that persistent trigger replaces another one but does not
        -- restore replace trigger after rollback.
        trigger.set(event_name, trigger_name, origin_trigger)
        box.begin()
        box.schema.func.create(trigger_name,
            {language = 'lua', trigger = event_name})
        t.assert_not_equals(trigger.info(event_name)[event_name][1][2],
                            origin_trigger)
        box.rollback()
        t.assert_equals(trigger.info(event_name), {})

        -- Check that deletion before rollback does not break tarantool
        box.begin()
        box.schema.func.create(trigger_name,
            {language = 'lua', trigger = event_name})
        t.assert_equals(#trigger.info(event_name)[event_name], 1)
        trigger.del(event_name, trigger_name)
        t.assert_equals(trigger.info(event_name), {})
        box.rollback()
        t.assert_equals(trigger.info(event_name), {})
    end)
end

g.test_several_events = function()
    g.server:exec(function()
        local trigger = require('trigger')
        local event_names = {'event1', 'event2', 'event3'}
        local trigger_name = 'test.several_events'
        local origin_trigger = {"I am an origin trigger!"}
        local mt = {}
        mt.__call = function() end
        setmetatable(origin_trigger, mt)

        for _, event_name in pairs(event_names) do
            t.assert_equals(trigger.info(event_name), {})
        end
        box.schema.func.create(trigger_name,
            {language = 'lua', trigger = event_names})
        for _, event_name in pairs(event_names) do
            t.assert_equals(#trigger.info(event_name)[event_name], 1)
        end

        box.schema.func.drop(trigger_name)
        for _, event_name in pairs(event_names) do
            t.assert_equals(trigger.info(event_name), {})
        end
    end)
end

-- Test that both persistent and non-persistent functions are called correctly
-- by module trigger.
g.test_correct_call = function()
    g.server:exec(function()
        local trigger = require('trigger')
        local event_names = {'test.call[1]', 'test.call[2]', 'test.call[3]'}
        local trigger_names = {'test.call[2]', 'test.call[1]'}
        rawset(_G, 'state', {})
        local bodies = {
            'function(a, b, c, d) table.insert(state, {2, a, b, c, d}) end',
            'function(a, b, c, d) table.insert(state, {1, a, b, c, d}) end'
        }
        for i = 1, #bodies do
            box.schema.func.create(trigger_names[i],
                {language = 'lua', trigger = event_names, body = bodies[i]})
        end
        rawset(_G, 'not_persistent_func', function(a, b, c, d)
            table.insert(_G.state, {0, a, b, c, d})
        end)
        box.schema.func.create('not_persistent_func',
            {language = 'lua', trigger = event_names})

        for i = 1, #event_names do
            trigger.call(event_names[i])
            t.assert_equals(_G.state, {{0}, {1}, {2}})
            _G.state = {}

            trigger.call(event_names[i], 'a', nil, 'b', 10)
            t.assert_equals(_G.state, {
                {0, 'a', nil, 'b', 10},
                {1, 'a', nil, 'b', 10},
                {2, 'a', nil, 'b', 10}
            })
            _G.state = {}
        end

        rawset(_G, 'state', nil)
        rawset(_G, 'not_persistent_func', nil)
    end)
end
