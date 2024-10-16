local datetime = require('datetime')

local t = require('luatest')

---@class luatest.group
local tg = t.group('logger_context', {
    {log_level = 'debug'},
    {log_level = 'verbose'},
    {log_level = 'info'},
    {log_level = 'warn'},
    {log_level = 'error'},
})

local FILENAME = "app-luatest/logger_context_test.lua"
local MODULE = "app-luatest.logger_context_test"

local function generate_message()
    -- Just a one-liner to generate a pseudo-random string
    return tostring({}):sub(10)
end

-- This function checks that the actual log entry fields are equal to the
-- expected ones. For some fields special checks are performed.
local assert_log_entry_fields_values = function(actual, expected)
    local actual_table = require('json').decode(actual)

    -- The time field is checked with a 60 seconds tolerance.
    if expected.time then
        t.assert(actual_table.time, "time field is present")

        local actual_time = datetime.parse(actual_table.time).timestamp
        local expected_time = datetime.parse(expected.time).timestamp

        t.assert_almost_equals(actual_time, expected_time, 60,
            "time field value is correct")

        expected.time = nil
        actual_table.time = nil
    end

    -- The pid field is checked to be a number, the actual value is not
    -- checked.
    if expected.pid then
        t.assert_type(actual_table.pid, "number",
            "pid is number")
        expected.pid = nil
        actual_table.pid = nil
    end

    -- The fiber_id field is checked to be a number, the actual value is not
    -- checked.
    if expected.fiber_id then
        t.assert_type(actual_table.fiber_id, "number",
            "fiber_id is number")
        expected.fiber_id = nil
        actual_table.fiber_id = nil
    end

    -- The file field is checked to contain the expected value, the actual
    -- value can contain longer path.
    if expected.file then
        t.assert_str_contains(actual_table.file, expected.file, false,
            "file is correct")
        expected.file = nil
        actual_table.file = nil
    end

    -- The line field is checked to be a number, the actual value is not
    -- checked.
    if expected.line then
        t.assert_type(actual_table.line, "number",
            "line is number")
        expected.line = nil
        actual_table.line = nil
    end

    -- The module field is checked to contain the expected value, the actual
    -- value can contain longer name.
    if expected.module then
        t.assert_str_contains(actual_table.module, expected.module, false,
            "module is correct")
        expected.module = nil
        actual_table.module = nil
    end

    -- The rest of the fields are checked to be equal to the expected ones.
    t.assert_equals(actual_table, expected, "All fields are correct")
end

-- Simple json fields order check. Based on the JSON specification.
-- https://www.json.org/json-en.html
local assert_log_entry_fields_order = function(actual, expected)
    -- Log entry is a JSON object, so it should start with '{' and any number
    -- of whitespaces.
    local expected_string = "{%s*"

    -- In our case the value can be a string or a number,
    -- so the regex is used to match a string with optional quotes.
    -- The value can be padded with whitespaces.
    local value_regex = "%s*\"?[^\"]*\"?%s*"

    local comma_needed = false
    for _, name in ipairs(expected) do
        if comma_needed then
            -- Members of an object are separated by commas with optional
            -- whitespaces.
            expected_string = expected_string .. ",%s*"
        else
            comma_needed = true
        end

        -- Member consists of a name (string), followed by optional whitespaces,
        -- a colon, and a value.
        expected_string = ("%s\"%s\"%s%s"):format(
            expected_string, name, "%s*:", value_regex)
    end

    -- The object is closed with a '}'. Before that there can be any number of
    -- unsorted members.
    expected_string = expected_string .. ".*}"

    t.assert_str_contains(
        actual, expected_string, true, "Expected fields order")
end

local default_log_entry_fields_order = {
    "time",
    "level",
    "message",
    "pid",
    "cord_name",
    "fiber_id",
    "fiber_name",
    "file",
    "line",
    "module",
}

local server = require('luatest.server'):new({
    box_cfg = {
        log_format = 'json',
    }
})

t.before_suite(function()
    server:start()
end)

t.after_suite(function()
    server:drop()
end)

tg.after_each(function()
    server:exec(function()
        box.cfg({
            log_level = 'info',
            log_context_generator = box.NULL,
            log_fields_order = box.NULL,
        })
        rawset(_G, 'MY_CONTEXT_GENERATOR', nil)
        box.schema.func.drop('MY_CONTEXT_GENERATOR', {if_exists = true})
    end)
end)

-- This test checks that the default context is correct.
-- Should pass even on tarantool versions older than 3.3.
tg.test_default_context = function(g)
    local message = generate_message()

    local expected_entry = {
        time = datetime.now():format(),
        level = g.params.log_level:upper(),
        message = message,
        pid = true,
        cord_name = "main",
        fiber_id = true,
        fiber_name = "main",
        file = FILENAME,
        line = true,
        module = MODULE,
    }

    server:exec(function(log_level, msg)
        box.cfg({log_level = log_level})
        require('log')[log_level]({message = msg})
    end, {g.params.log_level, message})

    local log_entry = server:grep_log((".*%s.*"):format(message))

    assert_log_entry_fields_order(log_entry, default_log_entry_fields_order)
    assert_log_entry_fields_values(log_entry, expected_entry)
end

tg.test_empty_context = function(g)
    local message = generate_message()

    local expected_entry = {
        message = message,
    }

    server:exec(function(log_level, msg)
        rawset(_G, 'MY_CONTEXT_GENERATOR', function() return {} end)
        box.cfg({
            log_level = log_level,
            log_context_generator = 'MY_CONTEXT_GENERATOR',
        })
        require('log')[log_level]({message = msg})
    end, {g.params.log_level, message})

    local log_entry = server:grep_log((".*%s.*"):format(message))

    assert_log_entry_fields_values(log_entry, expected_entry)
end

tg.test_stored_context_generator = function(g)
    local message = generate_message()

    local expected_entry = {
        message = message,
        foo = "bar",
    }

    server:exec(function(log_level, msg)
        box.schema.func.create('MY_CONTEXT_GENERATOR', {body = [[
            function()
                return {
                    foo = "bar",
                }
            end
        ]]})

        box.cfg({
            log_level = log_level,
            log_context_generator = 'MY_CONTEXT_GENERATOR',
        })
        require('log')[log_level]({message = msg})
    end, {g.params.log_level, message})

    local log_entry = server:grep_log((".*%s.*"):format(message))

    assert_log_entry_fields_values(log_entry, expected_entry)
end

tg.test_custom_context = function(g)
    local message = generate_message()

    local expected_entry = {
        time = datetime.now():format(),
        level = g.params.log_level:upper(),
        message = message,
        pid = true,
        cord_name = "main",
        fiber_id = true,
        fiber_name = "main",
        file = FILENAME,
        line = true,
        module = MODULE,
        foo = "bar",
        baz = "qux",
        uuid = server:get_instance_uuid(),
    }

    server:exec(function(log_level, msg)
        rawset(_G, 'MY_CONTEXT_GENERATOR', function(get_default_context)
            local ctx = get_default_context()
            ctx.foo = "bar"
            ctx.baz = "qux"
            ctx.uuid = box.info.uuid
            return ctx
        end)
        box.cfg({
            log_level = log_level,
            log_context_generator = 'MY_CONTEXT_GENERATOR',
        })
        require('log')[log_level]({message = msg})
    end, {g.params.log_level, message})

    local log_entry = server:grep_log((".*%s.*"):format(message))

    assert_log_entry_fields_order(log_entry, default_log_entry_fields_order)
    assert_log_entry_fields_values(log_entry, expected_entry)
end

tg.test_custom_fields_order = function(g)
    local message = generate_message()

    local fields_order = {
        "file",
        "line",
        "foo",
        "message",
    }

    local expected_order = {
        "file",
        "line",
        "message",
    }

    local expected_entry = {
        time = datetime.now():format(),
        level = g.params.log_level:upper(),
        message = message,
        pid = true,
        cord_name = "main",
        fiber_id = true,
        fiber_name = "main",
        file = FILENAME,
        line = true,
        module = MODULE,
    }

    server:exec(function(log_level, msg, order)
        box.cfg({
            log_level = log_level,
            log_fields_order = order,
        })
        require('log')[log_level]({message = msg})
    end, {g.params.log_level, message, fields_order})

    local log_entry = server:grep_log((".*%s.*"):format(message))

    assert_log_entry_fields_order(log_entry, expected_order)
    assert_log_entry_fields_values(log_entry, expected_entry)
end

tg.test_custom_context_and_order = function(g)
    local message = generate_message()

    local expected_entry = {
        time = datetime.now():format(),
        uuid = server:get_instance_uuid(),
        message = message,
        foo = "bar",
        baz = "qux",
    }

    local fields_order = {
        "uuid",
        "message",
        "foo",
    }

    server:exec(function(log_level, msg, order)
        rawset(_G, 'MY_CONTEXT_GENERATOR', function()
            return {
                foo = "bar",
                baz = "qux",
                time = require('datetime').now():format(),
                uuid = box.info.uuid,
            }
        end)
        box.cfg({
            log_level = log_level,
            log_context_generator = 'MY_CONTEXT_GENERATOR',
            log_fields_order = order,
        })
        require('log')[log_level]({message = msg})
    end, {g.params.log_level, message, fields_order})

    local log_entry = server:grep_log((".*%s.*"):format(message))

    assert_log_entry_fields_order(log_entry, fields_order)
    assert_log_entry_fields_values(log_entry, expected_entry)
end

tg.test_custom_context_and_order_log_new = function(g)
    local message = generate_message()

    local expected_entry = {
        time = datetime.now():format(),
        uuid = server:get_instance_uuid(),
        message = message,
        foo = "bar",
        baz = "qux",
    }

    local fields_order = {
        "uuid",
        "message",
        "foo",
    }

    server:exec(function(log_level, msg, order)
        rawset(_G, 'MY_CONTEXT_GENERATOR', function()
            return {
                foo = "bar",
                baz = "qux",
                time = require('datetime').now():format(),
                uuid = box.info.uuid,
            }
        end)
        box.cfg({
            log_level = log_level,
            log_context_generator = 'MY_CONTEXT_GENERATOR',
            log_fields_order = order,
        })

        require('log').new('my_logger')[log_level]({message = msg})
    end, {g.params.log_level, message, fields_order})

    local log_entry = server:grep_log((".*%s.*"):format(message))

    assert_log_entry_fields_order(log_entry, fields_order)
    assert_log_entry_fields_values(log_entry, expected_entry)
end
