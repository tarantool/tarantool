local t = require('luatest')
local compat = require('compat')
local console = require('console')
local json = require('json')
local yaml = require('yaml')
local tarantool = require('tarantool')

local g = t.group()

-- Test the `prev' argument to the table constructor of `box.error.new'
-- (gh-9103).
g.test_error_new_prev_arg = function()
    local e1 = box.error.new({"errare"})
    local e2 = box.error.new({"humanum", prev = e1})
    local _, e3 = pcall(box.error, {"est", prev = e2})

    t.assert_equals(e3.prev.prev.message, "errare")
    t.assert_equals(e3.prev.message, "humanum")
    t.assert_equals(e3.message, "est")

    t.assert_error_msg_equals(
        "Invalid argument 'prev' (error expected, got number)",
        box.error.new, {prev = 333})
end

-- Test custom error payload (gh-9104).
g.test_custom_payload = function()
    local uuid = require('uuid')
    local decimal = require('decimal')
    local datetime = require('datetime')
    local varbinary = require('varbinary')

    local e = box.error.new({
        reason = "Something happened",
        -- Non-string key is ignored.
        [12] = 34,
        -- Custom payload fields.
        foo = 42,
        bar = "abc",
    })
    -- Check that payload fields can be accessed with the dot operator.
    t.assert_equals(e.foo, 42)
    t.assert_equals(e.bar, "abc")
    -- Ignore the `trace' field, which contains trimmed filename.
    local unpacked = e:unpack()
    unpacked.trace = nil
    t.assert_equals(unpacked, {
        base_type = "ClientError",
        type = "ClientError",
        message = "Something happened",
        code = 0,
        foo = 42,
        bar = "abc",
    })

    -- Test `box.error()' with payload fields of various types.
    local my_uuid = uuid.new()
    local _, e = pcall(box.error, {
        type = "MyAppError",
        reason = "Keyboard not found, press F1 to continue",
        num = 42,
        str = "abc",
        arr = {1, 2, 3},
        map = {a = 1, b = 2},
        uuid = my_uuid,
        decimal = decimal.new('1.23'),
        datetime = datetime.new{year = 2000},
        interval = datetime.interval.new{min = 10},
        varbinary = varbinary.new('foo'),
    })
    t.assert_covers(e:unpack(), {
        num = 42,
        str = "abc",
        arr = {1, 2, 3},
        map = {a = 1, b = 2},
        uuid = my_uuid,
        decimal = 1.23,
        datetime = datetime.new{year = 2000},
        interval = datetime.interval.new{min = 10},
        varbinary = varbinary.new('foo'),
    })

    -- Test that built-in error fields can not be overridden.
    e = box.error.new({
        reason = "Non-system disk or disk error",
        code = 0xC0DE,
        base_type = "MyBaseType",
        type = "MyAppError",
        custom_type = "MyCustomType",
        errno = "MyErrno",
        trace = "MyTrace",
    })
    unpacked = e:unpack()
    unpacked.trace = nil
    t.assert_equals(unpacked, {
        message = "Non-system disk or disk error",
        code = 0xC0DE,
        base_type = "CustomError",
        type = "MyAppError",
        custom_type = "MyAppError",
    })
end

-- Test the `message` argument to the table constructor of `box.error.new`
-- (gh-9102).
g.test_error_new_message_arg = function()
    local msg = 'message'
    t.assert_equals(box.error.new{msg}.message, msg)
    t.assert_equals(box.error.new{message = msg}.message, msg)
    t.assert_equals(box.error.new{reason = msg}.message, msg)
    t.assert_equals(box.error.new{msg, message = 'other'}.message, msg)
    t.assert_equals(box.error.new{message = msg, reason = 'other'}.message, msg)
    t.assert_equals(box.error.new{msg, reason = 'other'}.message, msg)
end

-- Test that error's methods are not masked by the payload fields.
g.test_methods_masking = function()
    local e = box.error.new({
        unpack = 'hack',
        raise = 'hack',
        match = 'hack',
        set_prev = 'hack',
        __serialize = 'hack',
    })
    t.assert_type(e.unpack, 'function')
    t.assert_type(e.raise, 'function')
    t.assert_type(e.match, 'function')
    t.assert_type(e.set_prev, 'function')
    t.assert_type(e.__serialize, 'function')
    t.assert_equals(e:unpack().unpack, 'hack')
    t.assert_equals(e:unpack().raise, 'hack')
    t.assert_equals(e:unpack().match, 'hack')
    t.assert_equals(e:unpack().set_prev, 'hack')
    t.assert_equals(e:unpack().__serialize, 'hack')
end

-- Test that payload fields of a cause are inherited by the effect (gh-9106).
g.test_payload_inheritance = function()
    local e1 = box.error.new({foo = 11, bar = 22})
    local e2 = box.error.new({bar = 33, baz = 44, prev = e1})
    local e3 = box.error.new({baz = 55, prev = e2})

    t.assert_equals(e3.foo, 11) -- Inherited from e1.
    t.assert_equals(e3.bar, 33) -- Inherited from e2.
    t.assert_equals(e3.baz, 55) -- Own payload field.
end

g.after_test('test_unpack', function()
    compat.box_error_unpack_type_and_code = 'default'
end)

-- Test that redundant fields of error:unpack() are not serialized (gh-9101).
g.test_unpack = function()
    t.assert(compat.box_error_unpack_type_and_code.default, 'old')

    compat.box_error_unpack_type_and_code = 'old'
    local u = box.error.new{type = 'MyError', reason = 'foobar'}:unpack()
    t.assert_equals(u.base_type, 'CustomError')
    t.assert_equals(u.custom_type, 'MyError')
    t.assert_equals(u.code, 0)
    u = box.error.new(box.error.UNSUPPORTED, 'foo', 'bar'):unpack()
    t.assert_equals(u.base_type, 'ClientError')
    t.assert_equals(u.custom_type, nil)
    t.assert_equals(type(u.code), 'number')

    compat.box_error_unpack_type_and_code = 'new'
    u = box.error.new{type = 'MyError', reason = 'foobar'}:unpack()
    t.assert_equals(u.base_type, nil)
    t.assert_equals(u.custom_type, nil)
    t.assert_equals(u.code, nil)
    u = box.error.new(box.error.UNSUPPORTED, 'foo', 'bar'):unpack()
    t.assert_equals(u.base_type, nil)
    t.assert_equals(u.custom_type, nil)
    t.assert_equals(type(u.code), 'number')
end

local prevprev = box.error.new{reason = "prevprev"}
local prev = box.error.new{reason = "prev", prev = prevprev}
local cur = box.error.new{reason = "cur", prev = prev}

-- Test the increased `box.error` serialization verbosity (gh-9105).
g.test_increased_error_serialization_verbosity = function()
    t.assert_equals(cur:__serialize(), tostring(cur))
    t.assert_equals(yaml.encode(cur), yaml.encode(cur.message))
    t.assert_equals(json.encode(cur), json.encode(cur.message))

    t.assert_equals(compat.box_error_serialize_verbose.default, 'old')
    compat.box_error_serialize_verbose = 'new'

    local prevprev_serialized = prevprev:__serialize()
    local prev_serialized = prev:__serialize()
    local cur_serialized = cur:__serialize()

    t.assert_equals(yaml.encode(cur), yaml.encode(cur_serialized))
    t.assert_equals(json.encode(cur), json.encode(cur_serialized))

    -- Test that serialization of error without `prev` field is equivalent to
    -- `unpack`.
    t.assert_equals(prevprev_serialized, prevprev:unpack())
    -- Test that serialization of `prev` field works correctly.
    t.assert_equals(prev_serialized.prev, prevprev_serialized)
    t.assert_equals(cur_serialized.prev, prev_serialized)
    -- Test that serialization of error with `prev` field is equivalent to
    -- `unpack` except for the `prev` field.
    local prev_unpacked = prev:unpack()
    prev_serialized.prev = nil
    prev_unpacked.prev = nil
    t.assert_equals(prev_serialized, prev_unpacked)
    local cur_unpacked = cur:unpack()
    cur_serialized.prev = nil
    cur_unpacked.prev = nil
    t.assert_equals(cur_serialized, cur_unpacked)
end

g.after_test('test_increased_error_serialization_verbosity', function()
    compat.box_error_serialize_verbose = 'default'
end)

-- Test the increased `box.error` string conversion verbosity (gh-9105).
g.test_increased_error_string_conversion_verbosity = function()
    t.assert_equals(tostring(cur), cur.message)

    t.assert_equals(compat.box_error_serialize_verbose.default, 'old')
    compat.box_error_serialize_verbose = 'new'

    local prevprev_unpacked = prevprev:unpack()
    prevprev_unpacked.message = nil
    prevprev_unpacked = json.encode(prevprev_unpacked)
    t.assert_equals(tostring(prevprev),
                    string.format("%s %s",
                                  prevprev.message, prevprev_unpacked))

    local prev_unpacked = prev:unpack()
    prev_unpacked.message = nil
    prev_unpacked.prev = nil
    prev_unpacked = json.encode(prev_unpacked)
    local cur_unpacked = cur:unpack()
    cur_unpacked.message = nil
    cur_unpacked.prev = nil
    cur_unpacked = json.encode(cur_unpacked)
    t.assert_equals(tostring(cur),
                    string.format("%s %s\n" ..
                                  "%s %s\n" ..
                                  "%s %s",
                                  prevprev.message, prevprev_unpacked,
                                  prev.message, prev_unpacked,
                                  cur.message, cur_unpacked))
end

g.after_test('test_increased_error_string_conversion_verbosity', function()
    compat.box_error_serialize_verbose = 'default'
end)

-- Test ClientError arguments become payload fields (gh-9109).
g.test_client_error_creation = function()
    t.skip_if(not tarantool.build.test_build, 'build is not test one')

    -- We don't check types when add payload from Lua.
    local e = box.error.new(box.error.TEST_TYPE_CHAR, {x = 1, y = {}})
    t.assert_equals(e.field, {x = 1, y = {}})

    -- Test passing not all error payload fields works too.
    local e = box.error.new(box.error.TEST_5_ARGS, 1, 2)
    t.assert_equals(e.f1, 1)
    t.assert_equals(e.f2, 2)
    t.assert_equals(e.f3, nil)
    t.assert_equals(e.f4, nil)
    t.assert_equals(e.f5, nil)

    -- Test passing excess payload fields works too.
    local e = box.error.new(box.error.TEST_TYPE_INT, 1, 2, 3, 4, 5)
    t.assert_equals(e.field, 1)

    -- Test format string is supported in message.
    local e = box.error.new(box.error.TEST_FORMAT_MSG, 1, 'two')
    t.assert_equals(e.message, 'Test error 1 two')
    t.assert_equals(e.f1, 1)
    t.assert_equals(e.f2, 'two')

    -- Test number of arguments of format string may be less
    -- then number of payload arguments.
    local e = box.error.new(box.error.TEST_FORMAT_MSG_FEWER, 1, 'five', 3)
    t.assert_equals(e.message, 'Test error 1 five')
    t.assert_equals(e.f1, 1)
    t.assert_equals(e.f2, 'five')
    t.assert_equals(e.f3, 3)

    -- Test if field name is "" then respective positional argument
    -- is printed in formatted string message but not become payload.
    --
    -- We don't support creation of errors with 'l' modifiers in format string.
    local fields_count = function(e)
        local c = 0
        for _, _ in pairs(e:unpack()) do c = c + 1 end
        return c
    end
    local ref = box.error.new(box.error.TEST_FIRST)
    local e = box.error.new(box.error.TEST_OMIT_TYPE_CHAR, string.byte('x'))
    t.assert_equals(e.message, 'Test error x')
    t.assert_equals(fields_count(e), fields_count(ref))
    local e = box.error.new(box.error.TEST_OMIT_TYPE_INT, 1)
    t.assert_equals(e.message, 'Test error 1')
    t.assert_equals(fields_count(e), fields_count(ref))
    local e = box.error.new(box.error.TEST_OMIT_TYPE_UINT, 2)
    t.assert_equals(e.message, 'Test error 2')
    t.assert_equals(fields_count(e), fields_count(ref))
    local e = box.error.new(box.error.TEST_OMIT_TYPE_STRING, 'str')
    t.assert_equals(e.message, 'Test error str')
    t.assert_equals(fields_count(e), fields_count(ref))
end

--
-- Get tab completion for @s
-- @param s string
-- @return tab completion
local function tabcomplete(s)
    return console.completion_handler(s, 0, #s)
end

-- Test `box.error` autocompletion (gh-9107).
g.test_autocomplete = function()
    -- tabcomplete always uses global table
    rawset(_G, 'err', box.error.new{'cur', foo = 777,
                                    prev = box.error.new{'prev', bar = 777}})

    local r = tabcomplete('err.')
    t.assert_items_equals(r, {
        'err.',
        'err.raise(',
        'err.foo',
        'err.base_type',
        'err.prev',
        'err.message',
        'err.match(',
        'err.set_prev(',
        'err.trace',
        'err.type',
        'err.unpack(',
        'err.code',
        'err.bar',
    })

    r = tabcomplete('err:')
    t.assert_items_equals(r, {
        'err:',
        'err:match(',
        'err:raise(',
        'err:set_prev(',
        'err:unpack(',
    })
end

-- Test that box.error{code = ..} generates message from errcode (gh-9876).
g.test_error_message_with_named_code_arg = function()
    local code = box.error.TRANSACTION_CONFLICT
    local message = 'Transaction has been aborted by conflict'
    local unk_code = 100500
    local unk_message = 'Unknown error'
    local type = 'ClientError'

    -- Reference behavior.
    t.assert_equals(box.error.new(code).message, message)
    t.assert_equals(box.error.new(code).type, type)
    t.assert_equals(box.error.new(unk_code).message, unk_message)
    t.assert_equals(box.error.new(unk_code).type, type)

    -- Usage of named arguments must lead to similar result.
    t.assert_equals(box.error.new{code = code}.message, message)
    t.assert_equals(box.error.new{code = code}.type, type)
    t.assert_equals(box.error.new{code = unk_code}.message, unk_message)
    t.assert_equals(box.error.new{code = unk_code}.type, type)

    -- By default the code is 0, so it's Unknown error.
    t.assert_equals(box.error.new{}.message, unk_message)
    t.assert_equals(box.error.new{}.type, type)

    -- Explicit specifying or message must have priority.
    t.assert_equals(box.error.new{code = code, reason = 'wtf'}.message, 'wtf')
    t.assert_equals(box.error.new{code = code, reason = 'wtf'}.type, type)
    t.assert_equals(box.error.new{code = code, message = 'wtf'}.message, 'wtf')
    t.assert_equals(box.error.new{code = code, message = 'wtf'}.type, type)

    -- Custom errors are not affected.
    t.assert_equals(box.error.new{type = 'My'}.message, '')
    t.assert_equals(box.error.new{type = 'My'}.type, 'My')
    t.assert_equals(box.error.new{type = 'My', code = code}.message, '')
    t.assert_equals(box.error.new{type = 'My', code = code}.type, 'My')
    t.assert_equals(box.error.new{type = 'My', code = code,
                                  reason = 'wtf'}.message, 'wtf')
    t.assert_equals(box.error.new{type = 'My', code = code,
                                  reason = 'wtf'}.type, 'My')
end
