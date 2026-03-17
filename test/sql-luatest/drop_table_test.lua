local server = require('luatest.server')
local t = require('luatest')

local g = t.group("drop_table", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        local sql = [[SET SESSION "sql_default_engine" = '%s';]]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_drop_table = function(cg)
    cg.server:exec(function()
        -- create space.
        local sql = "CREATE TABLE zzzoobar "..
                    "(c1 INT, c2 INT PRIMARY KEY, c3 TEXT, c4 INT);"
        box.execute(sql)

        box.execute("CREATE INDEX zb ON zzzoobar(c1, c3);")

        -- Dummy entry.
        box.execute("INSERT INTO zzzoobar VALUES (111, 222, 'c3', 444);")

        box.execute("DROP TABLE zzzoobar")

        -- Table does not exist anymore. Should error here.
        local exp_err = "Space 'zzzoobar' does not exist"
        sql = "INSERT INTO zzzoobar VALUES (111, 222, 'c3', 444);"
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)
    end)
end

-- gh-3712: if space features sequence, data from _sequence_data
-- must be deleted before space is dropped.
g.test_3712_delete_before_space_drop = function(cg)
    cg.server:exec(function()
        box.execute("CREATE TABLE t1 (id INT PRIMARY KEY AUTOINCREMENT);")
        box.execute("INSERT INTO t1 VALUES (NULL);")
        local res = box.snapshot()
        t.assert_equals(res, 'ok')
    end)
    cg.server:restart()
    -- Cleanup.
    -- DROP TABLE should do the job.
    cg.server:exec(function()
        box.execute("DROP TABLE t1;")
    end)
end
