local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'insertion_crash'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_insertion_crash_1 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(i INT PRIMARY KEY, a INT);]])
        local sql = [[INSERT INTO t(i) SELECT a, i FROM t;]]
        local res = [[2 values for 1 columns]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
        box.execute([[DROP TABLE t;]])
    end)
end

g.test_insertion_crash_2 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(i INT PRIMARY KEY, a INT);]])
        local sql = [[INSERT INTO t SELECT a, i, 1 FROM t;]]
        local res = [[table T has 2 columns but 3 values were supplied]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
        box.execute([[DROP TABLE t;]])
    end)
end
