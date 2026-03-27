local server = require('luatest.server')
local t = require('luatest')

local g = t.group("select", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        local sql = [[SET SESSION "sql_default_engine" = '%s']]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- These tests are aimed at checking transitive transactions
-- between SQL and Lua.
-- gh-4157: autoincrement within transaction started in SQL
-- leads to seagfault.
--
g.test_4157_transitive_transaction = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t (id INT PRIMARY KEY AUTOINCREMENT);]])
        box.execute([[START TRANSACTION;]])
        box.execute([[INSERT INTO t VALUES (null), (null);]])
        box.execute([[INSERT INTO t VALUES (null), (null);]])
        box.execute([[SAVEPOINT sp;]])
        box.execute([[INSERT INTO t VALUES (null);]])
        box.execute([[ROLLBACK TO sp;]])
        local _, err = box.execute([[INSERT INTO t VALUES (null);]])
        t.assert_equals(err, nil)
        box.commit()

        local res = box.space.t:select()
        t.assert_equals(res, {{1}, {2}, {3}, {4}, {6}})

        box.space.t:drop()
    end)
end
