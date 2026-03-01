local server = require('luatest.server')
local t = require('luatest')

local g = t.group("drop_index", {{engine = 'memtx'}, {engine = 'vinyl'}})

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

g.test_drop_index = function(cg)
    cg.server:exec(function()
        -- create space
        box.execute([[CREATE TABLE zzoobar (c1 NUMBER, c2 INT PRIMARY KEY,
                                            c3 TEXT, c4 NUMBER);]])

        box.execute("CREATE UNIQUE INDEX zoobar2 ON zzoobar(c1, c4);")
        box.execute("CREATE INDEX zoobar3 ON zzoobar(c3);")

        -- Dummy entry
        box.execute("INSERT INTO zzoobar VALUES (111, 222, 'c3', 444);")

        box.execute("DROP INDEX zoobar2 ON zzoobar;")
        box.execute("DROP INDEX zoobar3 On zzoobar;")

        -- zoobar2 is dropped - should be OK
        local sql = "INSERT INTO zzoobar VALUES (111, 223, 'c3', 444);"
        local res = box.execute(sql)
        t.assert_equals(res, {row_count = 1})

        -- zoobar2 was dropped. Re-creation should  be OK
        res = box.execute("CREATE INDEX zoobar2 ON zzoobar(c3);")
        t.assert_equals(res, {row_count = 1})

        -- Cleanup
        box.execute("DROP INDEX zoobar2 ON zzoobar;")
        box.execute("DROP TABLE zzoobar;")
    end)
end
