local t = require('luatest')
local g = t.group('gh-9997-update-splice')
local varbinary = require('varbinary')

-- Simple test that test several variants of update splice operation.
g.test_tuple_update_splice = function()
    local tuple
    -- Varbinary field.
    tuple = box.tuple.new{varbinary.new('1234567890')}
    tuple = tuple:update{{':', 1, 4, 2, 'abc'}}
    t.assert(varbinary.is(tuple[1]))
    t.assert_equals(tuple[1], '123abc67890')

    -- Varbinary argument.
    tuple = box.tuple.new{'1234567890'}
    tuple = tuple:update{{':', 1, 4, 2, varbinary.new('abc')}}
    t.assert_type(tuple[1], 'string')
    t.assert_equals(tuple[1], '123abc67890')

    -- Varbinary field and argument.
    tuple = box.tuple.new{varbinary.new('1234567890')}
    tuple = tuple:update{{':', 1, 4, 2, varbinary.new('abc')}}
    t.assert(varbinary.is(tuple[1]))
    t.assert_equals(tuple[1], '123abc67890')

    -- Other types are still forbidden.
    local mess = "Argument type in operation ':' on field 1 does not match " ..
                 "field type: expected a string or varbinary"
    tuple = box.tuple.new{1234567890}
    t.assert_error_msg_equals(mess, tuple.update, tuple,
                              {{':', 1, 4, 2, 'abc'}})
    tuple = box.tuple.new{'1234567890'}
    t.assert_error_msg_equals(mess, tuple.update, tuple,
                              {{':', 1, 4, 2, 42}})
end
