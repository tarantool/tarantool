local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'map_array_problems'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

-- Make sure that DISTINCT cannot be used with ARRAY or MAP.
g.test_distinct = function()
    g.server:exec(function()
        local t = require('luatest')
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
