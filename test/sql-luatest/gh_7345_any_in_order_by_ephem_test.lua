local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'test_any_in_order_by_ephem'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_any_in_order_by_ephem = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(i INT PRIMARY KEY, a ANY, b INTEGER);]])
        box.execute([[INSERT INTO t VALUES(1, [1, 2], 2);]])
        box.execute([[INSERT INTO t VALUES(2, {'a': 1, 'b': 2}, 1);]])
        local sql = [[SELECT a FROM t ORDER BY b LIMIT 1;]]
        t.assert_equals(box.execute(sql).rows, {{{a = 1, b = 2}}})
        local sql = [[SELECT a FROM t ORDER BY b LIMIT 1 OFFSET 1;]]
        t.assert_equals(box.execute(sql).rows, {{{1, 2}}})
        box.execute([[DROP TABLE t;]])
    end)
end
