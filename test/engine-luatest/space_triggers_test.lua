local server = require('luatest.server')
local t = require('luatest')

-- Since event name depends on space name, test with small and huge names
-- Large name size is chosen to be larger than static buf size
local g = t.group('Basic triggers', t.helpers.matrix{
    engine = {'vinyl', 'memtx'},
    space_name = {'a', string.rep('a', 20000)}
})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
end)

g.after_each(function(cg)
    cg.server:exec(function(space_name)
        local trigger = require('trigger')
        if box.space[space_name] then box.space[space_name]:drop() end
        -- Delete all registered triggers
        local trigger_info = trigger.info()
        for event, trigger_list in pairs(trigger_info) do
            for _, trigger_descr in pairs(trigger_list) do
                local trigger_name = trigger_descr[1]
                trigger.del(event, trigger_name)
            end
        end
    end, {cg.params.space_name})
end)

g.test_on_replace = function(cg)
    cg.server:exec(function(engine, space_name)
        local trigger = require('trigger')
        local old_state = {}
        local new_state = {}
        local handlers = {}
        for i = 1, 4 do
            table.insert(handlers, function(old_tuple, new_tuple, space, req)
                if old_tuple ~= nil then
                    table.insert(old_state, old_tuple[i])
                end
                table.insert(new_state, new_tuple[i])
                t.assert_equals(space, space_name)
                t.assert_equals(req, "REPLACE")
            end)
        end
        local space_id = 743
        local event_by_id = string.format('box.space[%d].on_replace', space_id)
        local event_by_name = string.format('box.space.%s.on_replace',
            space_name)
        trigger.set(event_by_id, tostring(2), handlers[2])
        trigger.set(event_by_name, tostring(4), handlers[4])
        local s = box.schema.create_space(space_name,
            {id = space_id, engine = engine})
        s:create_index('pk')
        t.assert_equals(old_state, {})
        t.assert_equals(new_state, {})
        s:replace{0, 5, 10, 15}
        t.assert_equals(old_state, {})
        t.assert_equals(new_state, {5, 15})
        new_state = {}

        trigger.set(event_by_id, tostring(1), handlers[1])
        trigger.set(event_by_name, tostring(3), handlers[3])
        s:replace{1, 2, 3, 4}
        t.assert_equals(old_state, {})
        t.assert_equals(new_state, {1, 2, 3, 4})
        new_state = {}

        s:replace{1, 3, 5, 7}
        t.assert_equals(old_state, {1, 2, 3, 4})
        t.assert_equals(new_state, {1, 3, 5, 7})
        old_state = {}
        new_state = {}

        local new_tuple = {11, 12, 13, 14}
        trigger.call(event_by_id, nil, new_tuple, space_name, "REPLACE")
        trigger.call(event_by_name, nil, new_tuple, space_name, "REPLACE")
        t.assert_equals(new_state, new_tuple)
        new_state = {}
    end, {cg.params.engine, cg.params.space_name})
end

g.test_before_replace = function(cg)
    cg.server:exec(function(engine, space_name)
        local trigger = require('trigger')
        local handlers = {}
        local old_state = {}
        for i = 1, 4 do
            table.insert(handlers, function(old_tuple, new_tuple, space, req)
                if old_tuple ~= nil then
                    table.insert(old_state, old_tuple[i])
                end
                local prev_field = new_tuple[i]
                t.assert_equals(space, space_name)
                t.assert_equals(req, "REPLACE")
                return new_tuple:update{{'+', i + 1, prev_field}}
            end)
        end
        local space_id = 743
        local event_by_id = string.format('box.space[%d].before_replace',
            space_id)
        local event_by_name = string.format('box.space.%s.before_replace',
            space_name)
        trigger.set(event_by_id, tostring(2), handlers[2])
        trigger.set(event_by_name, tostring(4), handlers[4])
        local s = box.schema.create_space(space_name,
            {id = space_id, engine = engine})
        s:create_index('pk')

        s:replace{1, 3, 7, 19, 289}
        t.assert_equals(old_state, {})
        t.assert_equals(s:get(1), {1, 3, 3 + 7, 19, 289 + 19})

        trigger.set(event_by_id, tostring(1), handlers[1])
        trigger.set(event_by_name, tostring(3), handlers[3])

        s:replace{2, 15, 30, 70, 193}
        t.assert_equals(old_state, {})
        t.assert_equals(s:get(2), {2, 17, 47, 117, 310})

        s:replace{1, 2, 3, 4, 5}
        t.assert_equals(old_state, {1, 3, 10, 19})
        t.assert_equals(s:get(1), {1, 3, 6, 10, 15})
        old_state = {}
    end, {cg.params.engine, cg.params.space_name})
end

-- Checks if returned nil and none works correctly
g.test_before_replace_return_nil_or_none = function(cg)
    cg.server:exec(function(engine, space_name)
        local trigger = require('trigger')
        t.assert_equals(trigger.info(), {})
        local space_id = 743
        local event_by_id = string.format('box.space[%d].before_replace',
            space_id)
        trigger.set(event_by_id, 'my_trg', function() return nil end)
        local s = box.schema.create_space(space_name,
            {id = space_id, engine = engine})
        s:create_index('pk')

        s:replace{1, 1}
        t.assert_equals(s:get(1), nil)

        trigger.set(event_by_id, 'my_trg', function() return end)
        s:replace{1, 1}
        t.assert_equals(s:get(1), {1, 1})
    end, {cg.params.engine, cg.params.space_name})
end

g = t.group('Old API triggers', t.helpers.matrix{
    engine = {'vinyl', 'memtx'},
})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
end)

-- Checks if triggers set with old API are removed with the space.
g.test_old_api_triggers_are_temporary = function(cg)
    cg.server:exec(function(engine)
        local trigger = require('trigger')
        t.assert_equals(trigger.info(), {})
        local space_id = 743
        local on_replace_event = string.format('box.space[%d].on_replace',
            space_id)
        local before_replace_event =
            string.format('box.space[%d].before_replace', space_id)
        local s = box.schema.create_space('test1', {
            id = space_id, engine = engine,
            format = {{name = 'field1', type = 'unsigned'}}
        })
        s:create_index('pk')

        local all_triggers = {}
        local non_space_triggers = {}
        local function space_set_trigger(new, old, name)
            local h = s:on_replace(new, old, name)
            s:before_replace(new, old, name)
            table.insert(all_triggers, 1, {name, new})
            return h
        end
        local function event_set_trigger(name, handler)
            local h = trigger.set(on_replace_event, name, handler)
            trigger.set(before_replace_event, name, handler)
            table.insert(all_triggers, 1, {name, handler})
            table.insert(non_space_triggers, 1, {name, handler})
            return h
        end
        local function check_space_triggers(expected)
            -- Space shows only handlers.
            local expected_handlers = {}
            for _, trigger_info in pairs(expected) do
                table.insert(expected_handlers, trigger_info[2])
            end
            t.assert_equals(s:on_replace(), expected_handlers)
            t.assert_equals(s:before_replace(), expected_handlers)
        end
        local function check_event_triggers(expected)
            local info = trigger.info(on_replace_event)[on_replace_event]
            t.assert_equals(info, expected)
            info = trigger.info(before_replace_event)[before_replace_event]
            t.assert_equals(info, expected)
        end

        -- Insert and remove trigger.
        local h = space_set_trigger(function() end, nil)
        space_set_trigger(nil, h)
        all_triggers = {}

        space_set_trigger(function() end, nil, "space_trigger1")
        event_set_trigger("event_trigger1", function() end)
        space_set_trigger(function() end, nil, "space_trigger2")
        event_set_trigger("event_trigger2", function() end)

        space_set_trigger(function() end, nil, "space_replaced_with_event")
        event_set_trigger("space_replaced_with_event", function() end)
        table.remove(all_triggers, 2)

        event_set_trigger("event_replaced_with_space", function() end)
        space_set_trigger(function() end, nil, "event_replaced_with_space")
        -- The last trigger was replaced with an old space API.
        table.remove(non_space_triggers, 1)
        table.remove(all_triggers, 2)

        space_set_trigger(function() end, nil, "space_trigger3")

        check_space_triggers(all_triggers)
        check_event_triggers(all_triggers)

        s:alter({
            name = 'test2',
            format = {{name = 'field1', type = 'unsigned'}}
        })
        check_space_triggers(all_triggers)
        check_event_triggers(all_triggers)

        s:drop()
        check_event_triggers(non_space_triggers)

    end, {cg.params.engine})
end

g = t.group('Recovery triggers', {
    -- Vinyl does not recover from snapshot.
    {engine = 'vinyl', snapshot = false},
    {engine = 'memtx', snapshot = true},
    {engine = 'memtx', snapshot = false},
})

g.before_each(function(cg)
    -- Do not create snapshots automatically
    cg.server = server:new({box_cfg = {checkpoint_interval = 0}})
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:stop()
end)

g.test_recovery_replace = function(cg)
    cg.server:exec(function(engine, snapshot)
        local s = box.schema.create_space('test', {engine = engine})
        s:create_index('pk')
        for i = 1, 10 do
            s:insert{i, i - 2}
        end
        if snapshot then
            box.snapshot()
        end
    end, {cg.params.engine, cg.params.snapshot})
    local run_before_cfg = [[
        local trigger = require('trigger')
        local msgpack = require('msgpack')
        local regular_events = {
            'box.space[512].before_replace',
            'box.space.test.before_replace',
            'box.space[512].on_replace',
            'box.space.test.on_replace'
        }
        -- Ckeck that regular triggers does not work on recovery
        rawset(_G, 'regular_fired', false)
        for i = 1, 4 do
            trigger.set(regular_events[i], 'regular_check',
                        function() rawset(_G, 'regular_fired', true) end)
        end

        local recovery_events = {
            'box.space[512].before_recovery_replace',
            'box.space.test.before_recovery_replace',
            'box.space[512].on_recovery_replace',
            'box.space.test.on_recovery_replace'
        }

        rawset(_G, 'test_states', {})
        for i = 1, 4 do
            trigger.set(recovery_events[i], 'test_trigger',
                        function(old, new, space, req, header, body)
                local states = rawget(_G, 'test_states')
                assert(msgpack.is_object(header))
                assert(msgpack.is_object(body))
                -- Check translation of `box.iproto.key' constants.
                assert(header.lsn ~= nil)
                assert(header.LSN ~= nil)
                assert(body.space_id == 512)
                assert(body.SPACE_ID == 512)
                table.insert(states, {i,
                    {old, new, space, req, header:decode(), body:decode()}
                })
                new = new:update{{"+", 2, 1}}
                return new
            end)
        end
    ]]
    cg.server:restart({
        env = {
            ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = run_before_cfg,
        }
    })
    cg.server:exec(function(recover_from_snapshot)
        t.assert_equals(rawget(_G, 'regular_fired'), false)
        local states = rawget(_G, 'test_states')
        t.assert_equals(#states, 40)
        -- Check that tuples were updated
        for i = 1, 10 do
            t.assert_equals(box.space.test:get(i), {i, i})
        end
        for i = 1, 10 do
            -- Check order of triggers execution
            t.assert_equals(states[4 * i - 3][1], 1)
            t.assert_equals(states[4 * i - 2][1], 2)
            t.assert_equals(states[4 * i - 1][1], 3)
            t.assert_equals(states[4 * i][1], 4)

            -- Check passed arguments
            local first = 4 * i - 3
            local last = 4 * i
            for j = first, last do
                local tuple = {i, i}
                local req_tuple = tuple
                if j == first then
                    tuple = {i, i - 2}
                    req_tuple = tuple
                elseif j == first + 1 then
                    tuple = {i, i - 1}
                    req_tuple = {i, i - 2}
                end
                t.assert_equals(states[j][2][2], tuple)
                t.assert_equals(states[j][2][6][box.iproto.key.TUPLE],
                    req_tuple)
                states[j][2][2] = nil
                states[j][2][6][box.iproto.key.TUPLE] = nil
            end
            -- All arguments instead of new_tuple and tuple in request must be
            -- the same for the same request
            for j = 1, 3 do
                t.assert_equals(states[j][2], states[j + 1][2])
            end

            -- Inspect xrow
            local header = states[4 * i][2][5]
            t.assert_not_equals(header, nil)
            t.assert_not_equals(header[box.iproto.key.TIMESTAMP], nil)
            t.assert_equals(header[box.iproto.key.REQUEST_TYPE],
                box.iproto.type.INSERT)
            t.assert_not_equals(header[box.iproto.key.LSN], nil)
            if not recover_from_snapshot then
                t.assert_not_equals(header[box.iproto.key.REPLICA_ID], nil)
            end
            local body = states[4 * i][2][6]
            t.assert_equals(body[box.iproto.key.SPACE_ID], 512)
        end
        -- Check that recovery triggers are not fired after recovery
        box.space.test:replace{0, 0}
        t.assert_equals(#states, 40)
    end, {cg.params.snapshot})
end
