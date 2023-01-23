local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'test_prepared_stmt_truncation'})
    g.server:start()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(i INT PRIMARY KEY);]])
        box.execute([[INSERT INTO t VALUES(1);]])
    end)
end)

g.after_all(function()
    g.server:exec(function()
        box.execute([[DROP TABLE t;]])
    end)
    g.server:stop()
end)

g.test_prepared_stmt_truncation_1 = function()
    g.server:exec(function()
        local stmt = box.prepare([[SELECT * FROM t;]])
        box.execute([[TRUNCATE TABLE t;]])
        t.assert_equals(box.execute(stmt.stmt_id).rows, {})
    end)
end

g.test_prepared_stmt_truncation_2 = function()
    g.server:exec(function()
        local stmt = box.prepare([[INSERT INTO t VALUES(?);]])
        box.execute([[TRUNCATE TABLE t;]])
        t.assert_equals(box.execute(stmt.stmt_id, {1}), {row_count = 1})
    end)
end
