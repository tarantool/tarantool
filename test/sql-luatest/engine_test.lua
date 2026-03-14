local server = require('luatest.server')
local t = require('luatest')

local g = t.group("eng")

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Check errors during function create process
g.test_engine = function(cg)
    cg.server:exec(function()
        local sql = [[SET SESSION "sql_default_engine" = 'vinyl';]]
        box.execute(sql)

        box.execute("CREATE TABLE t1_vinyl(a INT PRIMARY KEY, b INT, c INT);")
        box.execute("CREATE TABLE t2_vinyl(a INT PRIMARY KEY, b INT, c INT);")

        sql = [[SET SESSION "sql_default_engine" = 'memtx';]]
        box.execute(sql)

        box.execute("CREATE TABLE t3_memtx(a INT PRIMARY KEY, b INT, c INT);")

        t.assert_equals(box.space.t1_vinyl.engine, 'vinyl')
        t.assert_equals(box.space.t2_vinyl.engine, 'vinyl')
        t.assert_equals(box.space.t3_memtx.engine, 'memtx')

        box.execute("DROP TABLE t1_vinyl;")
        box.execute("DROP TABLE t2_vinyl;")
        box.execute("DROP TABLE t3_memtx;")

        -- Name of engine considered to be string literal, so should be
        -- lowercased and quoted.
        --
        local exp_err = "Syntax error at line 2 near 'VINYL'"
        local sql = [[CREATE TABLE t1_vinyl (id INT PRIMARY KEY)
                      WITH ENGINE = VINYL;]]
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Syntax error at line 2 near 'vinyl'"
        sql = [[CREATE TABLE t1_vinyl (id INT PRIMARY KEY)
                WITH ENGINE = vinyl;]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Space engine 'VINYL' does not exist"
        sql = [[CREATE TABLE t1_vinyl (id INT PRIMARY KEY)
                WITH ENGINE = 'VINYL';]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = [[Syntax error at line 2 near '"vinyl"']]
        sql = [[CREATE TABLE t1_vinyl (id INT PRIMARY KEY)
                WITH ENGINE = "vinyl";]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        -- Make sure that wrong engine name is handled properly.
        --
        exp_err = "Space engine 'abc' does not exist"
        sql = [[CREATE TABLE t_wrong_engine (id INT PRIMARY KEY)
                WITH ENGINE = 'abc';]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Failed to create space 't_long_engine_name': "..
                  "space engine name is too long"
        sql = [[CREATE TABLE t_long_engine_name (id INT PRIMARY KEY)
                WITH ENGINE = 'very_long_engine_name';]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)
    end)
end

-- gh-4422: allow to specify engine in CREATE TABLE statement.
g.test_4422_allow_specify_engine_in_create_table = function(cg)
    cg.server:exec(function()
        local sql = [[CREATE TABLE t1_vinyl (id INT PRIMARY KEY)
                      WITH ENGINE = 'vinyl';]]
        box.execute(sql)
        t.assert_equals(box.space.t1_vinyl.engine, 'vinyl')
        sql = [[CREATE TABLE t1_memtx (id INT PRIMARY KEY)
                WITH ENGINE = 'memtx';]]
        box.execute(sql)
        t.assert_equals(box.space.t1_memtx.engine, 'memtx')

        local sql = [[SET SESSION "sql_default_engine" = 'vinyl';]]
        box.execute(sql)

        sql = [[CREATE TABLE t2_vinyl (id INT PRIMARY KEY)
                WITH ENGINE = 'vinyl';]]
        box.execute(sql)
        t.assert_equals(box.space.t2_vinyl.engine, 'vinyl')
        sql = [[CREATE TABLE t2_memtx (id INT PRIMARY KEY)
                WITH ENGINE = 'memtx';]]
        box.execute(sql)
        t.assert_equals(box.space.t2_memtx.engine, 'memtx')

        box.space.t1_vinyl:drop()
        box.space.t1_memtx:drop()
        box.space.t2_vinyl:drop()
        box.space.t2_memtx:drop()
    end)
end
