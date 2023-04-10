local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'gh-6668'})
    g.server:start()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end)
end)

g.after_all(function()
    g.server:stop()
end)

-- Make sure that DISTINCT cannot be used with ARRAY or MAP.
g.test_distinct = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE ta(i INT PRIMARY KEY, a ARRAY);]])
        box.execute([[INSERT INTO ta VALUES(1, [1]), (2, [2]);]])
        local sql = [[SELECT DISTINCT a FROM ta;]]
        local res = [[Failed to execute SQL statement: ]]..
                    [[field type 'array' is not comparable]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
        box.execute([[DROP TABLE ta;]])

        box.execute([[CREATE TABLE tm(i INT PRIMARY KEY, m map);]])
        sql = [[SELECT DISTINCT m FROM tm;]]
        res = [[Failed to execute SQL statement: ]]..
              [[field type 'map' is not comparable]]
        _, err = box.execute(sql)
        t.assert_equals(err.message, res)
        box.execute([[DROP TABLE tm;]])
    end)
end

-- Disallow incomparable values in GROUP BY.
g.test_group_by = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE ta(i INT PRIMARY KEY, a ARRAY);]])
        box.execute([[INSERT INTO ta VALUES(1, [1]), (2, [2]);]])
        local sql = [[SELECT a FROM ta GROUP BY a;]]
        local res = [[Type mismatch: can not convert array to comparable type]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
        box.execute([[DROP TABLE ta;]])

        box.execute([[CREATE TABLE tm(i INT PRIMARY KEY, m map);]])
        box.space.TM:insert({1, {a = 1}})
        box.space.TM:insert({2, {b = 2}})
        sql = [[SELECT m FROM tm GROUP BY m;]]
        res = [[Type mismatch: can not convert map to comparable type]]
        _, err = box.execute(sql)
        t.assert_equals(err.message, res)
        box.execute([[DROP TABLE tm;]])
    end)
end

--
-- Make sure that the error description for the incompatible right operand is
-- correct.
--
g.test_incomparable_comparison = function()
    g.server:exec(function()
        local sql = [[SELECT 1 > [1];]]
        local res = [[Type mismatch: can not convert array([1]) to ]]..
                    [[comparable type]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end

-- Make sure ORDER BY and GROUP BY do not support ARRAY and MAP type arguments.
g.test_order_by_group_by = function()
    g.server:exec(function()
        local err1 = "Type mismatch: can not convert array to comparable type"
        local err2 = "Type mismatch: can not convert map to comparable type"

        local res, err = box.execute([[SELECT 1 ORDER BY [1];]])
        t.assert(res == nil)
        t.assert_equals(err.message, err1)

        res, err = box.execute([[SELECT 1 GROUP BY [1];]])
        t.assert(res == nil)
        t.assert_equals(err.message, err1)

        res, err = box.execute([[SELECT 1 ORDER BY {1:1};]])
        t.assert(res == nil)
        t.assert_equals(err.message, err2)

        res, err = box.execute([[SELECT 1 GROUP BY {1:1};]])
        t.assert(res == nil)
        t.assert_equals(err.message, err2)
    end)
end
