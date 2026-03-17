local server = require('luatest.server')
local t = require('luatest')

local g = t.group("on_conflict", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        box.execute([[SET SESSION "sql_default_engine" = '%s']], {engine})
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)


-- Check that original sql ON CONFLICT clause is really
-- disabled.
g.test_on_conflict = function(cg)
    cg.server:exec(function()
        local exp_err = "At line 1 at or near position 58: "..
                        "keyword 'ON' is reserved. "..
                        "Please use double quotes if 'ON' is an identifier."
        local sql = "CREATE TABLE t (id INTEGER PRIMARY KEY, "..
                    "v INTEGER UNIQUE ON CONFLICT ABORT);"
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        sql = "CREATE TABLE q (id INTEGER PRIMARY KEY, "..
              "v INTEGER UNIQUE ON CONFLICT FAIL);"
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        sql = "CREATE TABLE p (id INTEGER PRIMARY KEY, "..
              "v INTEGER UNIQUE ON CONFLICT IGNORE);"
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        sql = "CREATE TABLE g (id INTEGER PRIMARY KEY, "..
              "v INTEGER UNIQUE ON CONFLICT REPLACE);"
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "At line 1 at or near position 40: "..
                  "keyword 'ON' is reserved. "..
                  "Please use double quotes if 'ON' is an identifier."
        sql = "CREATE TABLE e (id INTEGER PRIMARY KEY ON "..
              "CONFLICT REPLACE, v INTEGER);"
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "At line 1 at or near position 35: "..
                  "keyword 'ON' is reserved. "..
                  "Please use double quotes if 'ON' is an identifier."
        sql = "CREATE TABLE t1(a INT PRIMARY KEY ON CONFLICT REPLACE);"
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        sql = "CREATE TABLE t2(a INT PRIMARY KEY ON CONFLICT IGNORE);"
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        -- CHECK constraint is illegal with REPLACE option.
        --
        exp_err = "At line 1 at or near position 65: "..
                  "keyword 'ON' is reserved. "..
                  "Please use double quotes if 'ON' is an identifier."
        sql = "CREATE TABLE t (id INTEGER PRIMARY KEY, "..
              "a INTEGER CHECK (a > 5) ON CONFLICT REPLACE);"
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)
    end)
end

-- gh-3473: Primary key can't be declared with NULL.
g.test_3473_primary_key_not_declared_null = function(cg)
    cg.server:exec(function()
        local exp_err = "Primary index of space 'te17' "..
                        "can not contain nullable parts"
        local sql = "CREATE TABLE te17 (s1 INT NULL PRIMARY KEY NOT NULL);"
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute("CREATE TABLE te17 (s1 INT NULL PRIMARY KEY);")
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Failed to execute SQL statement: "..
                  "NULL declaration for column 'b' of table 'test' "..
                  "has been already set to 'none'"
        sql = "CREATE TABLE test (a int PRIMARY KEY, "..
              "b int NULL ON CONFLICT IGNORE);"
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Primary index of space 'test' can not contain nullable parts"
        sql = "CREATE TABLE test (a int, b int NULL, "..
              "c int, PRIMARY KEY(a, b, c));"
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        -- Several NOT NULL REPLACE constraints work.
        --
        sql = "CREATE TABLE a (id INT PRIMARY KEY, a INT NOT NULL "..
              "ON CONFLICT REPLACE DEFAULT 1, b INT NOT NULL ON "..
              "CONFLICT REPLACE DEFAULT 2);"
        box.execute(sql)
        box.execute("INSERT INTO a VALUES(1, NULL, NULL);")
        box.execute("INSERT INTO a VALUES(2, NULL, NULL);")

        local exp = {
            {1, 1, 2},
            {2, 1, 2},
        }
        local res = box.execute("SELECT * FROM a;")
        t.assert_equals(res.rows, exp)
        box.execute("DROP TABLE a;")
    end)
end

-- gh-3566: UPDATE OR IGNORE causes deletion of old entry.
g.test_3566_update_ignore_delection_old_entry = function(cg)
    cg.server:exec(function()
        local sql = "CREATE TABLE tj (s0 INT PRIMARY KEY, "..
                    "s1 INT UNIQUE, s2 INT);"
        box.execute(sql)
        box.execute("INSERT INTO tj VALUES (1, 1, 2), (2, 2, 3);")
        box.execute("CREATE UNIQUE INDEX i ON tj (s2);")
        box.execute("UPDATE OR IGNORE tj SET s1 = s1 + 1;")

        local exp = {
            {1, 2},
            {3, 3},
        }
        local res = box.execute("SELECT s1, s2 FROM tj;")
        t.assert_equals(res.rows, exp)

        box.execute("UPDATE OR IGNORE tj SET s2 = s2 + 1;")

        exp = {
            {1, 2},
            {3, 4},
        }
        res = box.execute("SELECT s1, s2 FROM tj;")
        t.assert_equals(res.rows, exp)
        box.execute("DROP TABLE tj;")
    end)
end

-- gh-3565: INSERT OR REPLACE causes assertion fault.
g.test_3565_insert_replace_assertion_fault = function(cg)
    cg.server:exec(function()
        box.execute("CREATE TABLE tj (s1 INT PRIMARY KEY, s2 INT);")
        box.execute("INSERT INTO tj VALUES (1, 2), (2, 3);")
        box.execute("CREATE UNIQUE INDEX i ON tj (s2);")
        box.execute("REPLACE INTO tj VALUES (1, 3);")

        local res = box.execute("SELECT * FROM tj;")
        t.assert_equals(res.rows, {{1, 3}})
        box.execute("INSERT INTO tj VALUES (2, 4), (3, 5);")
        box.execute("UPDATE OR REPLACE tj SET s2 = s2 + 1;")

        local exp = {
            {1, 4},
            {3, 6},
        }
        res = box.execute("SELECT * FROM tj;")
        t.assert_equals(res.rows, exp)

        box.execute("DROP TABLE tj;")
    end)
end
