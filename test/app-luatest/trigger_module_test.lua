local t = require('luatest')

local g = t.group()

local ffi_monotonic_id = 0

-- Helper that generates callable table, userdata and cdata
local function generate_callable_objects(handler)
    local ffi = require('ffi')
    local a = {}
    local mt = {__call = handler}
    setmetatable(a, mt)

    local b = newproxy(true)
    mt = getmetatable(b)
    mt.__call = handler

    ffi_monotonic_id = ffi_monotonic_id + 1
    local struct_name = string.format('foo%d', ffi_monotonic_id)
    ffi.cdef(string.format('struct %s { int x; }', struct_name))
    mt = {__call = handler}
    ffi.metatype(string.format('struct %s', struct_name), mt)
    local c = ffi.new(string.format('struct %s', struct_name), {x = 42})
    return a, b, c
end

-- Basic functionality - set, delete and call with function handler
g.test_simple = function()
    local trigger = require('trigger')

    local event = "my_events[15].subevent.event"
    local name = "my_triggers[4352].subtriggers.trigger"
    local state = 0
    local function handler(new_state)
        if new_state == nil then new_state = 1 end
        state = new_state
    end
    local ret_handler = trigger.set(event, name, handler)
    t.assert_equals(ret_handler, handler)
    trigger.call(event)
    t.assert_equals(state, 1)

    trigger.call(event, 42)
    t.assert_equals(state, 42)
    ret_handler = trigger.del(event, name)
    t.assert_equals(ret_handler, handler)

    state = 0
    trigger.call(event)
    t.assert_equals(state, 0)

    -- Check that deletion of nothing does not throw an error and returns nil
    ret_handler = trigger.del(event, name)
    t.assert_equals(ret_handler, nil)
end

-- Check that all the triggers are called in the reverse order
-- of their installation and share the object as argument
g.test_order_and_shared_argument = function()
    local trigger = require('trigger')

    local event = "my_event"
    local name_prefix = "my_triggers."
    local function h1(arg)
        t.assert_equals(arg, {})
        arg[1] = 1
    end
    local function h2(arg)
        t.assert_equals(arg, {1})
        arg[2] = 2
    end
    local function h3(arg)
        t.assert_equals(arg, {1, 2})
        arg[3] = 3
    end
    trigger.set(event, name_prefix .. '3', h3)
    trigger.set(event, name_prefix .. '2', h2)
    trigger.set(event, name_prefix .. '1', h1)
    local arg = {}
    trigger.call(event, arg)
    t.assert_equals(arg, {1, 2, 3})
    for i = 1, 3 do
        trigger.del(event, name_prefix .. tostring(i))
    end
end

-- Check that handler is not copied
g.test_shared_handler = function()
    local trigger = require('trigger')

    local event = "my_event"
    local name = "my_triggers"
    local state = 0
    local function handler(self)
        state = self.value
    end
    local a = {}
    local mt = {__call = handler}
    setmetatable(a, mt)
    trigger.set(event, name, a)
    for i = 1, 20 do
        a.value = i
        trigger.call(event)
        t.assert_equals(state, i)
    end

    trigger.del(event, name)
end

-- Check that both Lua and Tarantool exceptions are supported and
-- trigger.call method stops the execution after the first exception.
g.test_exceptions = function()
    local trigger = require('trigger')

    local event = "my_event"
    local name_prefix = "my_triggers."
    local lua_err = 'Just a Lua error'
    local tnt_err = 'Just a Tarantool error'
    local state = 0
    local function h1()
        state = state + 1
    end
    local function h2(cmd)
        if cmd == 'throw lua' or cmd == 'throw' then
            error(lua_err)
        end
    end
    local function h3(cmd)
        if cmd == 'throw tnt' or cmd == 'throw' then
            box.error{reason = tnt_err, errcode = 42}
        end
    end
    trigger.set(event, name_prefix .. '1', h1)
    trigger.call(event)
    t.assert_equals(state, 1)
    state = 0
    trigger.set(event, name_prefix .. '2', h2)
    local success = pcall(trigger.call, event)
    t.assert(success)
    t.assert_equals(state, 1)
    state = 0
    t.assert_error_msg_content_equals(lua_err, trigger.call, event, 'throw')
    t.assert_equals(state, 0)

    trigger.set(event, name_prefix .. '3', h3)
    success = pcall(trigger.call, event)
    t.assert(success)
    t.assert_equals(state, 1)
    state = 0
    t.assert_error_msg_content_equals(tnt_err, trigger.call, event, 'throw')
    t.assert_equals(state, 0)

    t.assert_error_msg_content_equals(lua_err, trigger.call, event, 'throw lua')
    t.assert_equals(state, 0)
    t.assert_error_msg_content_equals(tnt_err, trigger.call, event, 'throw tnt')
    t.assert_equals(state, 0)

    for i = 1, 3 do
        trigger.del(event, name_prefix .. tostring(i))
    end
end

-- Check that any callable object can be registered as a trigger
g.test_any_callable_trigger = function()
    local trigger = require('trigger')

    local event = "my_event"
    local name = "my_trigger"
    local state = 0
    local function handler(self, inc)
        if inc == nil then inc = 1 end
        state = state + inc
    end

    local function check_object(obj)
        local ret_handler = trigger.set(event, name, obj)
        t.assert_equals(ret_handler, obj)
        state = 0
        local new_state = math.random(1, 100)
        trigger.call(event, new_state)
        t.assert_equals(state, new_state)
        trigger.del(event, name)
    end

    local a, b, c = generate_callable_objects(handler)

    -- Callable table
    check_object(a)
    local mt = getmetatable(a)
    mt.__metatable = "Private"
    check_object(a)

    -- Callable userdata
    check_object(b)
    mt = getmetatable(b)
    mt.__metatable = "Private"
    check_object(a)

    -- Callable cdata
    check_object(c)
    -- Cdata metatable is already protected so a special check is not required
end

g.test_trigger_clear_from_trigger = function()
    local trigger = require('trigger')
    local event = "test_trigger_clear_event"
    local order = {}
    local f2 = function()
        table.insert(order, 2)
    end
    local f1 = function()
        table.insert(order, 1)
        trigger.del(event, 'f2')
        trigger.del(event, 'f1')
    end
    trigger.set(event, 'f2', f2)
    trigger.set(event, 'f1', f1)
    trigger.call(event)
    t.assert_equals(order, {1}, "Correct trigger invocation when 1st trigger \
                                 clears 2nd")
end

g.test_pairs = function()
    local trigger = require('trigger')

    local state = 0
    local function handler(inc)
        state = state + inc
    end
    local function handler_method(self, inc)
        state = state + inc
    end
    local a, b, c = generate_callable_objects(handler_method)
    local handlers = {handler, c, b, a, handler, c, a, handler, b, handler}
    local event = 'event_name'
    local trigger_prefix = 'trigger_name.'

    -- Event is empty so no triggers must be traversed.
    for _, _ in trigger.pairs(event) do
        t.assert(false)
    end

    for i = #handlers, 1, -1 do
        trigger.set(event, trigger_prefix..tostring(i),  handlers[i])
    end

    local idx = 0
    local sum = 0
    for name, h in trigger.pairs(event) do
        idx = idx + 1
        sum = sum + idx
        t.assert_equals(name, trigger_prefix .. tostring(idx))
        t.assert_equals(h, handlers[idx])
        h(idx)
    end
    t.assert_equals(idx, #handlers)
    t.assert_equals(state, sum)
    for i = 1, #handlers do
        trigger.del(event, trigger_prefix .. tostring(i))
    end
end

-- Check that a trigger deleted during pairs does not break anything and
-- isn't yielded by pairs after its deletion.
g.test_del_during_pairs = function()
    local trigger = require('trigger')

    local event = 'del_during_pairs_event'
    local trigger_num = 10
    local break_point = math.random(2, trigger_num - 1)
    local history = {}
    local function handler_method(self)
        table.insert(history, self.id)
    end
    local mt = {__call = handler_method}
    for i = trigger_num, 1, -1 do
        local a = {id = i}
        setmetatable(a, mt)
        trigger.set(event, tostring(i), a)
    end

    local idx = 0
    local iter = {trigger.pairs(event)}
    for _, handler in unpack(iter) do
        idx = idx + 1
        handler()
        if idx == break_point then
            break
        end
    end
    trigger.del(event, tostring(math.random(1, break_point)))
    trigger.del(event, tostring(break_point))
    local deleted_id = math.random(break_point + 1, trigger_num)
    trigger.del(event, tostring(deleted_id))

    for _, handler in unpack(iter) do
        handler()
    end

    t.assert_equals(#history, trigger_num - 1)
    local history_idx = 0
    for i = 1, trigger_num do
        if i ~= deleted_id then
            history_idx = history_idx + 1
            t.assert_equals(history[history_idx], i)
            trigger.del(event, tostring(i))
        end
    end
end

-- Check that pairs is successfully stopped after all triggers registered
-- on the event are dropped
g.test_clear_event_during_pairs = function()
    local trigger = require('trigger')

    local event = 'clear_during_pairs_event'
    local trigger_num = 10
    local break_point = math.random(2, trigger_num - 1)

    local history = {}
    local function handler_method(self)
        table.insert(history, self.id)
    end
    local mt = {__call = handler_method}
    for i = trigger_num, 1, -1 do
        local a = {id = i}
        setmetatable(a, mt)
        trigger.set(event, tostring(i), a)
    end

    local idx = 0
    local iter = {trigger.pairs(event)}
    for _, handler in unpack(iter) do
        idx = idx + 1
        handler()
        if idx == break_point then
            break
        end
    end

    for i = 1, trigger_num do
        trigger.del(event, tostring(i))
    end

    for _, handler in unpack(iter) do
        handler()
    end

    t.assert_equals(#history, break_point)
    for i = 1, break_point do
        t.assert_equals(history[i], i)
    end
end

g.test_invalid_args = function()
    local trigger = require('trigger')

    local valid_name = "name"

    local invalid_handlers = {nil, 42, 'abc', {}, {1, 2}, {k = 'v'}, box.NULL}
    for _, h in pairs(invalid_handlers) do
        local errmsg = string.format(
            "bad argument #3 to '?' (callable expected, got %s)", type(h))
        t.assert_error_msg_content_equals(errmsg, trigger.set, valid_name,
            valid_name, h)
    end

    local handler = function() return true end
    local invalid_names = {nil, {}, {1, 2}, {k = 'v'}, box.NULL, handler}
    for _, invalid_name in pairs(invalid_names) do
        local errmsg = string.format(
            "bad argument #2 to '?' (string expected, got %s)",
            type(invalid_name))
        t.assert_error_msg_content_equals(
            errmsg, trigger.set, valid_name, invalid_name, handler)
        t.assert_error_msg_content_equals(
            errmsg, trigger.del, valid_name, invalid_name)
        errmsg = string.format(
            "bad argument #1 to '?' (string expected, got %s)",
            type(invalid_name))
        t.assert_error_msg_content_equals(
            errmsg, trigger.set, invalid_name, valid_name, handler)
        t.assert_error_msg_content_equals(
            errmsg, trigger.del, invalid_name, valid_name)
        t.assert_error_msg_content_equals(errmsg, trigger.call, invalid_name)
        t.assert_error_msg_content_equals(errmsg, trigger.pairs, invalid_name)
        t.assert_error_msg_content_equals(errmsg, trigger.info, invalid_name)
    end
    local errmsg = "Usage: trigger.set(event, trigger, function)"
    t.assert_error_msg_content_equals(errmsg, trigger.set, 'event', 'trigger',
        handler, 'redundant')
    t.assert_error_msg_content_equals(errmsg, trigger.set, 'event', 'trigger')
    errmsg = "Usage: trigger.del(event, trigger)"
    t.assert_error_msg_content_equals(errmsg, trigger.del, 'event', 'trigger',
        'redundant')
    t.assert_error_msg_content_equals(errmsg, trigger.del, 'event')
    errmsg = "Usage: trigger.pairs(event)"
    t.assert_error_msg_content_equals(errmsg, trigger.pairs, 'event',
        'redundant')
    t.assert_error_msg_content_equals(errmsg, trigger.pairs)
    errmsg = "Usage: trigger.info([event])"
    t.assert_error_msg_content_equals(errmsg, trigger.info, 'event',
        'redundant')
    errmsg = "Usage: trigger.call(event, [args...])"
    t.assert_error_msg_content_equals(errmsg, trigger.call)
end

g.test_nil_args = function()
    local trigger = require('trigger')

    local event = "my_event"
    local name = "my_trigger"

    local state = {}
    local function handler(a1, a2, a3, a4)
        state = {a1, a2, a3, a4}
    end
    trigger.set(event, name, handler)
    trigger.call(event, 1, 2, nil)
    t.assert_equals(state, {1, 2})
    state = {}

    trigger.call(event, 1, nil, 3)
    t.assert_equals(state, {1, nil, 3})
    state = {}

    trigger.call(event, nil, 2, 3)
    t.assert_equals(state, {nil, 2, 3})
    state = {}

    trigger.call(event, nil, nil, 3)
    t.assert_equals(state, {nil, nil, 3})
    state = {}

    trigger.call(event, 1, nil, nil, 4)
    t.assert_equals(state, {1, nil, nil, 4})

    trigger.del(event, name)
end

g.test_info = function()
    local trigger = require('trigger')
    local state = 0
    local function handler(inc)
        state = state + inc
    end
    local function handler_method(self, inc)
        state = state + inc
    end
    local a, b, c = generate_callable_objects(handler_method)
    local trigger_info = {
        ["test_events[1].event"] = {
            {"my_triggers.trig1", a},
            {"my_triggers.trig2", b},
            {"trig1", c},
            {"trig2", handler},

        },
        ["test_event[2].EVENT"] = {
            {'a', a},
            {'b', b},
        },
        ["test_event[4].dsffsd"] = {
            {'c', c},
            {'h', handler},
        },
        ["AnUsualEventName"] = {
            {'triggers.1', a},
            {'triggers[1]', b},
        }
    }
    t.assert_equals(trigger.info(), {})
    for event_name, trigger_list in pairs(trigger_info) do
        for i = #trigger_list, 1, -1 do
            local trigger_data = trigger_list[i]
            local trigger_name = trigger_data[1]
            local trigger_handler = trigger_data[2]
            trigger.set(event_name, trigger_name, trigger_handler)
        end
    end
    for event_name, trigger_list in pairs(trigger_info) do
        local event_info = {[event_name] = trigger_list}
        t.assert_equals(trigger.info(event_name), event_info)
    end
    t.assert_equals(trigger.info(), trigger_info)
    for event_name, trigger_list in pairs(trigger_info) do
        for _, trigger_data in ipairs(trigger_list) do
            local trigger_name = trigger_data[1]
            trigger.del(event_name, trigger_name)
        end
    end
    t.assert_equals(trigger.info(), {})
end

-- Checks if empty events are never shown in info, even if its triggers
-- are not deleted yet (for example, when they are pinned by iterator).
g.test_info_no_empty_events = function()
    local trigger = require('trigger')
    local function handler() end
    local event_name = "test.event"
    local trigger_name = "test.trigger"
    t.assert_equals(trigger.info(), {})
    trigger.set(event_name, trigger_name, handler)

    local gen, state = trigger.pairs(event_name)
    local n, h = gen(state)
    t.assert_equals(n, trigger_name)
    t.assert_equals(h, handler)

    trigger.del(event_name, trigger_name)
    t.assert_equals(trigger.info(event_name), {})
    t.assert_equals(trigger.info(), {})
end
