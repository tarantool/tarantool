local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.schema.space.create('test', {id = 512})
        local function space_on_replace(...)
            return box.space.test:on_replace(...)
        end
        local function space_before_replace(...)
            return box.space.test:before_replace(...)
        end
        -- Every element of this table is an array of 2 elements. The first
        -- one is a function that sets triggers, and the second one is the name
        -- of the associated event in the trigger registry. The second element
        -- is optional.
        rawset(_G, 'old_api_triggers', {
            {box.session.on_connect, 'box.session.on_connect'},
            {box.session.on_disconnect, 'box.session.on_disconnect'},
            {box.session.on_auth, 'box.session.on_auth'},
            {space_on_replace, 'box.space[512].on_replace'},
            {space_before_replace, 'box.space[512].before_replace'},
            {box.ctl.on_election, 'box.ctl.on_election'},
            {box.ctl.on_recovery_state, 'box.ctl.on_recovery_state'},
            {box.ctl.on_schema_init, 'box.ctl.on_schema_init'},
        })

        rawset(_G, 'ffi_monotonic_id', 0)

        -- Helper that generates callable table, userdata and cdata
        local function generate_callable_objects(handler)
            local ffi = require('ffi')
            local a = {}
            local mt = {__call = handler}
            setmetatable(a, mt)

            local b = newproxy(true)
            mt = getmetatable(b)
            mt.__call = handler

            local ffi_id = rawget(_G, 'ffi_monotonic_id')
            rawset(_G, 'ffi_monotonic_id', ffi_id + 1)
            local struct_name = string.format('foo%d', ffi_id)
            ffi.cdef(string.format('struct %s { int x; }', struct_name))
            mt = {__call = handler}
            ffi.metatype(string.format('struct %s', struct_name), mt)
            local c = ffi.new(string.format('struct %s', struct_name), {x = 42})
            return a, b, c
        end
        rawset(_G, 'generate_callable_objects', generate_callable_objects)
    end)
end)

g.after_all(function()
    g.server:stop()
end)

g.test_old_api = function()
    g.server:exec(function()
        local old_api_triggers = rawget(_G, 'old_api_triggers')
        local generate_callable_objects =
            rawget(_G, 'generate_callable_objects')
        local function check_trigger(old_api_trigger)
            local function h1() end
            local h2, h3, h4 = generate_callable_objects(h1)

            t.assert_equals(old_api_trigger(), {})

            -- Invalid arguments
            t.assert_error_msg_content_equals(
                'trigger reset: incorrect arguments', old_api_trigger, nil,
                nil, {1, 2})
            t.assert_error_msg_content_equals(
                'trigger reset: incorrect arguments', old_api_trigger, nil,
                nil, h1)
            t.assert_error_msg_content_equals(
                'trigger reset: incorrect arguments', old_api_trigger, 'abc')
            t.assert_error_msg_content_equals(
                'trigger reset: incorrect arguments', old_api_trigger, nil,
                'abc')
            t.assert_error_msg_content_equals(
                'trigger reset: incorrect arguments', old_api_trigger, 'abc',
                'abc')
            t.assert_error_msg_content_equals(
                'trigger reset: incorrect arguments', old_api_trigger, 'abc',
                nil, 'name')
            t.assert_error_msg_content_equals(
                'trigger reset: Trigger is not found', old_api_trigger, nil, h1)

            -- Set and delete
            t.assert_equals(old_api_trigger(h1), h1)
            t.assert_equals(old_api_trigger(h2), h2)
            t.assert_equals(old_api_trigger(h3), h3)
            t.assert_equals(old_api_trigger(h4), h4)
            t.assert_equals(old_api_trigger(), {h4, h3, h2, h1})
            t.assert_equals(old_api_trigger(nil, h4), nil)
            t.assert_equals(old_api_trigger(), {h3, h2, h1})
            t.assert_equals(old_api_trigger(h2), h2)
            t.assert_equals(old_api_trigger(), {h3, h2, h1})
            t.assert_equals(old_api_trigger(h3, box.NULL, box.NULL), h3)
            t.assert_equals(old_api_trigger(), {h3, h2, h1})

            -- Replace
            -- Expected behavior: when both triggers has the same name, the
            -- order is preserved. Otherwise, old trigger is deleted and the new
            -- one is inserted at the beginning of the list. Also, when the
            -- names are different, trigger with the new name should be deleted
            -- to prevent name duplication.
            t.assert_equals(old_api_trigger(h4, h2), h4)
            t.assert_equals(old_api_trigger(), {h4, h3, h1})
            t.assert_equals(old_api_trigger(h1, h3), h1)
            t.assert_equals(old_api_trigger(), {h1, h4})
            t.assert_equals(old_api_trigger(h3), h3)
            t.assert_equals(old_api_trigger(), {h3, h1, h4})
            t.assert_equals(old_api_trigger(h1, h1), h1)
            t.assert_equals(old_api_trigger(), {h3, h1, h4})
            t.assert_equals(old_api_trigger(nil, h3), nil)
            t.assert_equals(old_api_trigger(), {h1, h4})
            t.assert_equals(old_api_trigger(box.NULL, h4), nil)
            t.assert_equals(old_api_trigger(), {h1})
            t.assert_equals(old_api_trigger(h2, h1), h2)
            t.assert_equals(old_api_trigger(), {h2})
            t.assert_equals(old_api_trigger(nil, h2, box.NULL), nil)
            t.assert_equals(old_api_trigger(), {})

            -- Name argument
            t.assert_equals(old_api_trigger(h1, nil, 't1'), h1)
            t.assert_equals(old_api_trigger(), {h1})
            -- Check if the second argument will be ignored
            t.assert_equals(old_api_trigger(h2, h1, 't2'), h2)
            t.assert_equals(old_api_trigger(), {h2, h1})
            t.assert_equals(old_api_trigger(h3, nil, 't1'), h3)
            t.assert_equals(old_api_trigger(), {h2, h3})
            t.assert_equals(old_api_trigger(nil, h1, 't1'), nil)
            t.assert_equals(old_api_trigger(), {h2})
            t.assert_equals(old_api_trigger(nil, nil, 't2'), nil)
            t.assert_equals(old_api_trigger(), {})
        end
        for _, trg_descr in pairs(old_api_triggers) do
            local trg = trg_descr[1]
            check_trigger(trg)
        end
    end)
end

g.test_key_value_args = function()
    g.server:exec(function()
        local old_api_triggers = rawget(_G, 'old_api_triggers')
        local generate_callable_objects =
            rawget(_G, 'generate_callable_objects')
        local function check_trigger(old_api_trigger)
            local function h1() end
            local h2, h3, h4 = generate_callable_objects(h1)

            t.assert_equals(old_api_trigger(), {})

            -- Invalid arguments
            t.assert_error_msg_content_equals(
                "trigger reset: incorrect arguments", old_api_trigger,
                {func = h1, name = 'trg'}, h1)
            t.assert_error_msg_content_equals(
                "trigger reset: incorrect arguments", old_api_trigger,
                {func = h1, name = 'trg'}, 'abc')
            t.assert_error_msg_content_equals(
                "func must be a callable object or nil", old_api_trigger,
                {func = 'str', name = 'trg'})
            t.assert_error_msg_content_equals(
                "name must be a string", old_api_trigger, {func = h1})
            t.assert_error_msg_content_equals(
                "name must be a string", old_api_trigger,
                {func = h1, name = box.NULL})

            -- Set and delete
            t.assert_equals(old_api_trigger{func = h1, name = 'h1'}, h1)
            t.assert_equals(old_api_trigger{func = h2, name = 'h2'}, h2)
            t.assert_equals(old_api_trigger{func = h3, name = 'h3'}, h3)
            t.assert_equals(old_api_trigger{func = h4, name = 'h4'}, h4)
            t.assert_equals(old_api_trigger(), {h4, h3, h2, h1})
            t.assert_equals(old_api_trigger{name = 'h4'}, nil)
            t.assert_equals(old_api_trigger(), {h3, h2, h1})
            t.assert_equals(old_api_trigger{name = 'h2', func = box.NULL}, nil)
            t.assert_equals(old_api_trigger(), {h3, h1})
            t.assert_equals(old_api_trigger{name = 'h3', func = box.NULL}, nil)
            t.assert_equals(old_api_trigger(), {h1})
            t.assert_equals(old_api_trigger{name = 'h1'}, nil)
            t.assert_equals(old_api_trigger(), {})
        end
        for _, trg_descr in pairs(old_api_triggers) do
            local trg = trg_descr[1]
            check_trigger(trg)
        end
    end)
end

g.test_associated_event = function()
    g.server:exec(function()
        local trigger = require('trigger')
        local old_api_triggers = rawget(_G, 'old_api_triggers')
        local function check_trigger(old_api_trigger, event_name)
            local trg = {"I am the trigger of event " .. event_name}
            local mt = {__call = function() end}
            setmetatable(trg, mt)
            old_api_trigger(trg)
            local event_triggers = trigger.info(event_name)[event_name]
            t.assert_equals(#event_triggers, 1)
            t.assert_equals(event_triggers[1][2], trg)
            old_api_trigger(nil, trg)
        end
        for _, trg_descr in pairs(old_api_triggers) do
            local trg = trg_descr[1]
            local event_name = trg_descr[2]
            if event_name ~= nil then
                check_trigger(trg, event_name)
            end
        end
    end)
end
