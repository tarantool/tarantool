local server = require('luatest.server')
local t = require('luatest')

local g = t.group("insert_unique", {{engine = 'memtx'}, {engine = 'vinyl'}})

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

g.test_insert_unique = function(cg)
    cg.server:exec(function()
        -- create space
        box.execute([[CREATE TABLE zoobar (c1 INT, c2 INT PRIMARY KEY,
                                           c3 TEXT, c4 INT);]])
        box.execute("CREATE UNIQUE INDEX zoobar2 ON zoobar(c1, c4);")

        -- Seed entry
        box.execute("INSERT INTO zoobar VALUES (111, 222, 'c3', 444);")

        -- PK must be unique
        local sql = "INSERT INTO zoobar VALUES (112, 222, 'c3', 444);"
        local _, err = box.execute(sql)
        local exp_err = 'Duplicate key exists in unique index ' ..
                        '"pk_unnamed_zoobar_1" in space "zoobar" ' ..
                        'with old tuple - [111, 222, "c3", 444] and ' ..
                        'new tuple - [112, 222, "c3", 444]'
        t.assert_equals(err.message, exp_err)

        -- Unique index must be respected
        _, err = box.execute("INSERT INTO zoobar VALUES (111, 223, 'c3', 444);")
        exp_err = 'Duplicate key exists in unique index "zoobar2" in space ' ..
                  '"zoobar" with old tuple - [111, 222, "c3", 444] and new ' ..
                  'tuple - [111, 223, "c3", 444]'
        t.assert_equals(err.message, exp_err)

        -- Cleanup
        box.execute("DROP INDEX zoobar2 ON zoobar;")
        box.execute("DROP TABLE zoobar;")
    end)
end
