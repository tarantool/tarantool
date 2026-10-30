local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group('Persistent triggers on a single node')

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
        -- restore replaced trigger after rollback.
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

-- Test if persisitent trigger cannot be dropped while it is running.
g.test_drop_while_running = function()
    g.server:exec(function()
        local trigger = require('trigger')
        local event_name = 'test.drop_while_running'
        local func_name = 'test_func'
        local errmsg = nil
        local function handler()
            local _, err = pcall(box.schema.func.drop, func_name)
            errmsg = err.message
        end
        rawset(_G, func_name, handler)
        box.schema.func.create(func_name,
            {language = 'lua', trigger = event_name})

        trigger.call(event_name)
        local expected_errmsg = string.format(
            "Can't drop function %d: function is referenced by trigger",
            box.func[func_name].id)
        t.assert_equals(errmsg, expected_errmsg)

        box.schema.func.drop(func_name)
    end)
end

-- Test that both persistent and non-persistent functions are called correctly
-- by module trigger.
g.test_call_from_module_trigger = function()
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

-- Test that deleted function cannot be used.
g.test_pairs_safety_in_module_trigger = function()
    g.server:exec(function()
        local trigger = require('trigger')
        local event_name = 'test.pairs_safety'
        local func_name = 'test_func'

        local func_call_count = 0
        local handler = function()
            func_call_count = func_call_count + 1
        end
        rawset(_G, func_name, handler)

        box.schema.func.create(func_name,
            {language = 'lua', trigger = event_name})

        local gen, param = trigger.pairs(event_name)
        local _, handler = gen(param)

        handler()
        t.assert_equals(func_call_count, 1, "Handler should be called normally")

        local errmsg = string.format(
            "Can't drop function 66: function is referenced by trigger",
            box.func[func_name].id)
        t.assert_error_msg_content_equals(errmsg, box.schema.func.drop,
            func_name)

        gen(param)
        handler()
        t.assert_equals(func_call_count, 2,
            "Handler is still alive even if not referenced by iterator")

        box.schema.func.drop(func_name)

        errmsg = "Trigger was deleted"
        t.assert_error_msg_content_equals(errmsg, handler)
        t.assert_equals(func_call_count, 2,
            "Handler was deleted - it mustn't be called")
    end)
end

local g = t.group('Persistent triggers in a cluster')

g.before_each(function()
    g.cluster = cluster:new({})
    local box_cfg = {replication = {
            server.build_listen_uri('server1', g.cluster.id),
            server.build_listen_uri('server2', g.cluster.id)
    }}
    g.server1 = g.cluster:build_server({alias = 'server1', box_cfg = box_cfg})
    g.server2 = g.cluster:build_server({alias = 'server2', box_cfg = box_cfg})

    g.cluster:add_server(g.server1)
    g.cluster:add_server(g.server2)
    g.cluster:start()
end)

g.after_each(function()
    g.cluster:drop()
end)

-- The test checks a scenario with a non-idempotent replicated trigger in
-- master-master replication. The trigger must be applied on each server.
-- To provide exactly-once semantics, we check `box.session.type()` in trigger.
g.test_replicated_non_idempotent_trigger = function(g)
    local expected_result = {
        {'cat', 6000},
        {'crocodile', 600000},
        {'dog', 10000},
        {'elephant', 4000000}
    }
    g.server1:exec(function()
        -- Transform kilogramms to gramms on actual replace, not replication
        local body = [[
            function(old_tuple, new_tuple)
                if box.session.type() ~= 'applier' then
                    return box.tuple.new{new_tuple[1], new_tuple[2] * 1000}
                end
            end
        ]]
        local event = 'box.space.weights.before_replace'
        box.schema.func.create('example.replicated_trigger',
            {body = body, trigger = event})
        box.schema.space.create('weights')
        box.space.weights:format({
            {name = 'name', type = 'string'},
            {name = 'gramms', type = 'unsigned'},
        })
        box.space.weights:create_index('primary', {parts = {'name'}})
        box.space.weights:replace{'elephant', 4000}
        box.space.weights:replace{'crocodile', 600}
    end)
    -- Wait for the trigger and the space to arrive on the second server
    g.server2:wait_for_vclock_of(g.server1)
    g.server2:exec(function()
        box.space.weights:replace{'cat', 6}
        box.space.weights:replace{'dog', 10}
    end)

    -- Wait for the writes to arrive on the first server
    g.server1:wait_for_vclock_of(g.server2)
    local check_case = function(expected_result)
        t.assert_equals(box.space.weights:select(nil, {fullscan=true}),
            expected_result)
    end
    g.server1:exec(check_case, {expected_result})
    g.server2:exec(check_case, {expected_result})
end
