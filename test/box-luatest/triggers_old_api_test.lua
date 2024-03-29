local server = require('luatest.server')
local t = require('luatest')
local pathjoin = require('fio').pathjoin

local g = t.group()

local libext = package.cpath:match('?.(%a+);')
local libpath = pathjoin(os.getenv('BUILDDIR'), 'test', 'box-luatest')
local env = {
    LUA_CPATH = ('%s/?.%s;%s;'):format(libpath, libext, os.getenv('LUA_CPATH')),
}

g.before_all(function()
    g.server = server:new({alias = 'master', env = env})
    g.server:start()
    g.server:exec(function()
        local net_box = require('net.box')
        local conn = net_box.connect("a:b")
        local function net_box_on_connect(...)
            return conn:on_connect(...)
        end
        local function net_box_on_disconnect(...)
            return conn:on_disconnect(...)
        end
        local function net_box_on_schema_reload(...)
            return conn:on_schema_reload(...)
        end
        -- There is an internal trigger initially - the test expects empty
        -- triggers in the beginning. Let's remove it - the triggers won't
        -- be fired anyway.
        local net_box_on_connect_trg = conn:on_connect()[1]
        conn:on_connect(nil, net_box_on_connect_trg)

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
        -- SWIM triggers cannot be tested because they use wrappers as triggers,
        -- so they should be tested in a separate test.
        rawset(_G, 'old_api_triggers', {
            {box.session.on_connect, 'box.session.on_connect'},
            {box.session.on_disconnect, 'box.session.on_disconnect'},
            {box.session.on_auth, 'box.session.on_auth'},
            {box.session.on_access_denied, 'box.session.on_access_denied'},
            {space_on_replace, 'box.space[512].on_replace'},
            {space_before_replace, 'box.space[512].before_replace'},
            {box.ctl.on_election, 'box.ctl.on_election'},
            {box.ctl.on_recovery_state, 'box.ctl.on_recovery_state'},
            {box.ctl.on_schema_init, 'box.ctl.on_schema_init'},
            {box.ctl.on_shutdown, 'box.ctl.on_shutdown'},
            {net_box_on_connect, nil},
            {net_box_on_disconnect, nil},
            {net_box_on_schema_reload, nil},
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
            -- Test inside transaction to make transactional triggers work
            box.begin()

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

            box.commit()
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
            -- Test inside transaction to make transactional triggers work
            box.begin()

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

            box.commit()
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

-- https://github.com/tarantool/tarantool/issues/9287
g.test_callable_number = function()
    g.server:exec(function()
        local old_api_triggers = rawget(_G, 'old_api_triggers')
        local trg = require('libcallnum')
        t.assert_type(trg, 'number')
        t.assert_type(trg(), 'number')
        local function f() end

        local function check_trigger(old_api_trigger)
            old_api_trigger(trg)
            t.assert_equals(old_api_trigger(), {trg})
            old_api_trigger(f, trg)
            t.assert_equals(old_api_trigger(), {f})
            old_api_trigger(trg, f)
            t.assert_equals(old_api_trigger(), {trg})
            old_api_trigger(trg, trg)
            t.assert_equals(old_api_trigger(), {trg})
            old_api_trigger(nil, trg)
            t.assert_equals(old_api_trigger(), {})
        end
        for _, trg_descr in pairs(old_api_triggers) do
            check_trigger(trg_descr[1])
        end
    end)
end

-- Triggers swim:on_member_event require a separate test since they accept ctx
-- and use wrappers as triggers.
g.test_swim_on_member_event = function()
    g.server:exec(function()
        local fiber = require('fiber')
        local swim = require('swim')
        local function uuid(i)
            local min_valid_prefix = '00000000-0000-1000-8000-'
            if i < 10 then
                return min_valid_prefix..'00000000000'..tostring(i)
            end
            assert(i < 100)
            return min_valid_prefix..'0000000000'..tostring(i)
        end
        local function uri(port)
            port = port or 0
            return '127.0.0.1:'..tostring(port)
        end

        local history = {}
        local ctx_history = {}
        local s = swim.new({generation = 0})

        s:on_member_event(function(_, _, ctx)
            table.insert(history, 1)
            table.insert(ctx_history, ctx)
        end, 'ctx1')

        s:on_member_event(function(_, _, ctx)
            table.insert(history, 2)
            table.insert(ctx_history, ctx)
        end, nil, {ctx = 'ctx2'})

        -- Each next trigger is inserted and then replaced.
        local t3 = s:on_member_event(function() end, nil, nil, 'ctx3')
        s:on_member_event(function(_, _, ctx)
            table.insert(history, 3)
            table.insert(ctx_history, ctx)
        end, t3, nil, 'ctx3')

        s:on_member_event(function() end, nil, 'named1', 'ctx4')
        s:on_member_event(function(_, _, ctx)
            table.insert(history, 4)
            table.insert(ctx_history, ctx)
        end, nil, 'named1', 'ctx4')

        s:on_member_event(function() end, 'ctx5', 'named2')
        s:on_member_event(function(_, _, ctx)
            table.insert(history, 5)
            table.insert(ctx_history, ctx)
        end, 'ctx5', 'named2')

        s:on_member_event{func = function() end, name = 'named3'}
        s:on_member_event{func = function(_, _, ctx)
            table.insert(history, 6)
            table.insert(ctx_history, ctx)
        end, name = 'named3', ctx = 'ctx6'}

        s:on_member_event(function() end, nil, 'named4')
        s:on_member_event(function(_, _, ctx)
            table.insert(history, 7)
            table.insert(ctx_history, ctx)
        end, nil, 'named4')

        s:cfg{uuid = uuid(1), uri = uri(), heartbeat_rate = 0.01}
        while #history < 1 do fiber.sleep(0) end
        t.assert_equals(history, {7, 6, 5, 4, 3, 2, 1})
        t.assert_equals(ctx_history,
            {'ctx6', 'ctx5', 'ctx4', 'ctx3', {ctx = 'ctx2'}, 'ctx1'})
    end)
end
