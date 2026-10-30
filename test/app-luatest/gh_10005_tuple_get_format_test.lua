local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Checks that `tuple:format` works as expected.
g.test_tuple_format = function()
    local f = box.tuple.format.new({{'id', 'number'}, {'name', 'string'}})
    local tuple = box.tuple.new({1, 'Flint'}, {format = f})
    t.assert_equals(type(tuple:format()), 'table')
    t.assert_equals(tuple:format(), f:totable())

    tuple = box.tuple.new({1, 'Flint', true}, {format = f})
    t.assert_equals(type(tuple:format()), 'table')
    t.assert_equals(tuple:format(), f:totable())
end

-- Checks that `box.tuple.format` works as expected.
g.test_box_tuple_format = function()
    local f = box.tuple.format.new({{'id', 'number'}, {'name', 'string'}})
    local tuple = box.tuple.new({1, 'Flint'}, {format = f})
    t.assert_equals(type(box.tuple.format(tuple)), 'table')
    t.assert_equals(box.tuple.format(tuple), f:totable())
    t.assert_equals(box.tuple.format(tuple), tuple:format())

    tuple = box.tuple.new({1, 'Flint', true}, {format = f})
    t.assert_equals(type(box.tuple.format(tuple)), 'table')
    t.assert_equals(box.tuple.format(tuple), f:totable())
    t.assert_equals(box.tuple.format(tuple), tuple:format())
end

-- Checks that `tuple:format` and `box.tuple.format` work as expected
-- without set format.
g.test_tuple_format_with_no_format = function()
    local tuple = box.tuple.new({1, 'Flint'})
    t.assert_equals(type(tuple:format()), 'table')
    t.assert_equals(tuple:format(), {})

    t.assert_equals(type(box.tuple.format(tuple)), 'table')
    t.assert_equals(box.tuple.format(tuple), {})
end
