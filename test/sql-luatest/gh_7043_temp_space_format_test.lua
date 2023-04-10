local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'gh-7043'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_test_space_format = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(i INT PRIMARY KEY, a STRING, b DOUBLE);]])
        box.execute([[INSERT INTO t VALUES(1, '1', 1.0);]])
        local sql = [[SELECT a, b, i FROM t ORDER BY a, b LIMIT 1;]]
        t.assert_equals(box.execute(sql).rows, {{'1', 1, 1}})
        box.execute([[DROP TABLE "t";]])
    end)
end
