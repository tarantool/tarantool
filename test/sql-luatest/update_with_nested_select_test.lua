local server = require('luatest.server')
local t = require('luatest')

local g = t.group("update", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s']]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_update_with_nested_select = function(cg)
    cg.server:exec(function ()
        -- create space
        box.execute([[CREATE TABLE t1(a INTEGER PRIMARY KEY,
                                      b INT UNIQUE, e INT);]])

        -- Seed entries
        box.execute("INSERT INTO t1 VALUES(1, 4, 6);")
        box.execute("INSERT INTO t1 VALUES(2, 5, 7);")

        -- Both entries must be updated
        local sql = "UPDATE t1 SET e = e + 1 WHERE b IN (SELECT b FROM t1);"
        t.assert_equals(box.execute(sql), {row_count = 2})

        -- Check
        t.assert_equals(box.execute("SELECT e FROM t1;").rows, {{7}, {8}})

        -- Cleanup
        box.execute("DROP TABLE t1;")
    end)
end
