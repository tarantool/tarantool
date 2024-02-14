local t = require('luatest')
local g = t.group()

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
        prev = "MyPrev",
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
