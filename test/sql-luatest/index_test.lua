local server = require('luatest.server')
local t = require('luatest')

local g = t.group("on_conflict", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        box.execute([[SET SESSION "sql_default_engine" = '%s';]], {engine})
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)


-- Check that original sql ON CONFLICT clause is really
-- disabled.
g.test_on_conflict = function(cg)
    cg.server:exec(function()
        local exp_err = "keyword 'ON' is reserved. "..
                        "Please use double quotes if 'ON' is an identifier."
        local sql = "CREATE TABLE t (id INTEGER PRIMARY KEY, "..
                    "v INTEGER UNIQUE ON CONFLICT ABORT);"
        local _, err = box.execute(sql)
        t.assert_str_contains(err.message, exp_err)

        sql = "CREATE TABLE q (id INTEGER PRIMARY KEY, "..
              "v INTEGER UNIQUE ON CONFLICT FAIL);"
        _, err = box.execute(sql)
                t.assert_str_contains(err.message, exp_err)

        sql = "CREATE TABLE p (id INTEGER PRIMARY KEY, "..
              "v INTEGER UNIQUE ON CONFLICT IGNORE);"
        _, err = box.execute(sql)
                t.assert_str_contains(err.message, exp_err)

        sql = "CREATE TABLE g (id INTEGER PRIMARY KEY, "..
              "v INTEGER UNIQUE ON CONFLICT REPLACE);"
        _, err = box.execute(sql)
                t.assert_str_contains(err.message, exp_err)

        exp_err = "keyword 'ON' is reserved. "..
                  "Please use double quotes if 'ON' is an identifier."
        sql = "CREATE TABLE e (id INTEGER PRIMARY KEY ON "..
              "CONFLICT REPLACE, v INTEGER);"
        _, err = box.execute(sql)
        t.assert_str_contains(err.message, exp_err)

        exp_err = "keyword 'ON' is reserved. "..
                  "Please use double quotes if 'ON' is an identifier."
        sql = "CREATE TABLE t1(a INT PRIMARY KEY ON CONFLICT REPLACE);"
        _, err = box.execute(sql)
        t.assert_str_contains(err.message, exp_err)

        sql = "CREATE TABLE t2(a INT PRIMARY KEY ON CONFLICT IGNORE);"
        _, err = box.execute(sql)
        t.assert_str_contains(err.message, exp_err)

        -- CHECK constraint is illegal with REPLACE option.
        exp_err = "keyword 'ON' is reserved. "..
                  "Please use double quotes if 'ON' is an identifier."
        sql = "CREATE TABLE t (id INTEGER PRIMARY KEY, "..
              "a INTEGER CHECK (a > 5) ON CONFLICT REPLACE);"
        _, err = box.execute(sql)
        t.assert_str_contains(err.message, exp_err)
    end)
end
