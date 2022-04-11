local t = require('luatest')
local g = t.group()
local yaml = require('yaml')

local function error_equals(e1, e2)
    local s1 = yaml.encode(e1:unpack())
    local s2 = yaml.encode(e2:unpack())
    return s1 == s2
end

g.test_copy_one = function()
    local e1 = box.error.new({code = 42, reason = 'testing', type = 'test_t'})
    local e2 = e1:copy_one()

    t.assert_not_equals(e1, e2)
    t.assert(error_equals(e1, e2))

    -- Check independency
    e1:set_prev(e2)
    t.assert_equals(e1.prev, e2)
    t.assert_equals(e2.prev, nil)
end

g.test_copy_all = function()
    local e1 = box.error.new({code = 1, reason = "1", type = 'test1'})
    local e2 = box.error.new({code = 2, reason = "2", type = 'test2'})
    local e3 = box.error.new({code = 3, reason = "3", type = 'test3'})
    local e4 = box.error.new({code = 4, reason = "4", type = 'test4'})
    e1:set_prev(e2)
    e2:set_prev(e3)
    e3:set_prev(e4)

    local copy = e1:copy_all()
    -- Check if the copied list doesn't contain any nodes from the
    -- original list and if it doesn't have any additional nodes.
    t.assert_not_equals(copy, nil)
    t.assert_not_equals(copy, e1)
    t.assert_not_equals(copy.prev, e2)
    t.assert_not_equals(copy.prev.prev, e3)
    t.assert_not_equals(copy.prev.prev.prev, e4)
    t.assert_equals(copy.prev.prev.prev.prev, nil)

    -- Check whether all errors are the same
    t.assert(error_equals(copy, e1))
    t.assert(error_equals(copy.prev, e2))
    t.assert(error_equals(copy.prev.prev, e3))
    t.assert(error_equals(copy.prev.prev.prev, e4))

    -- Check independency
    e3:set_prev(nil)
    t.assert_not_equals(copy.prev.prev.prev, nil)
    e2:set_prev(nil)
    t.assert_not_equals(copy.prev.prev, nil)
    e1:set_prev(nil)
    t.assert_not_equals(copy.prev, nil)
end
