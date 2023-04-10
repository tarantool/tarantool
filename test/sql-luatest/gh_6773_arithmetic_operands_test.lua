local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'gh-6773'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_add = function()
    g.server:exec(function()
        local res = [[Type mismatch: can not convert string('1') to ]]..
                    [[integer, decimal, double, datetime or interval]]
        local _, err = box.execute([[SELECT '1' + '2';]])
        t.assert_equals(err.message, res)
        _, err = box.execute([[SELECT 1 + '2';]])
        res = [[Type mismatch: can not convert string('2') to integer, ]]..
              [[decimal or double]]
        t.assert_equals(err.message, res)
    end)
end

g.test_sub = function()
    g.server:exec(function()
        local res = [[Type mismatch: can not convert string('1') to ]]..
                    [[integer, decimal, double, datetime or interval]]
        local _, err = box.execute([[SELECT '1' - '2';]])
        t.assert_equals(err.message, res)
        _, err = box.execute([[SELECT 1 - '2';]])
        res = [[Type mismatch: can not convert string('2') to integer, ]]..
              [[decimal or double]]
        t.assert_equals(err.message, res)
    end)
end

g.test_mul = function()
    g.server:exec(function()
        local res = [[Type mismatch: can not convert string('1') to ]]..
                    [[integer, decimal or double]]
        local _, err = box.execute([[SELECT '1' * '2';]])
        t.assert_equals(err.message, res)
        _, err = box.execute([[SELECT 1 * '2';]])
        res = [[Type mismatch: can not convert string('2') to integer, ]]..
              [[decimal or double]]
        t.assert_equals(err.message, res)
    end)
end

g.test_div = function()
    g.server:exec(function()
        local res = [[Type mismatch: can not convert string('1') to ]]..
                    [[integer, decimal or double]]
        local _, err = box.execute([[SELECT '1' / '2';]])
        t.assert_equals(err.message, res)
        _, err = box.execute([[SELECT 1 / '2';]])
        res = [[Type mismatch: can not convert string('2') to integer, ]]..
              [[decimal or double]]
        t.assert_equals(err.message, res)
    end)
end

g.test_rem = function()
    g.server:exec(function()
        local res = [[Type mismatch: can not convert string('1') to integer]]
        local _, err = box.execute([[SELECT '1' % '2';]])
        t.assert_equals(err.message, res)
        _, err = box.execute([[SELECT 1 % '2';]])
        res = [[Type mismatch: can not convert string('2') to integer]]
        t.assert_equals(err.message, res)
    end)
end
