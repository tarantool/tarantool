local server = require('luatest.server')
local t = require('luatest')

local g = t.group("primary_key", {{engine = 'memtx'}, {engine = 'vinyl'}})

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

-- All tables in SQL are now WITHOUT ROW ID, so if user
-- tries to create table without a primary key, an appropriate error message
-- should be raised. This tests checks it.
g.test_2929_primary_key = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t1(a INT PRIMARY KEY, b INT UNIQUE);]])

        local exp_err = [[Failed to create space 't2': PRIMARY KEY missing]]
        local _, err = box.execute([[CREATE TABLE t2(a INT UNIQUE, b INT);]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = [[Failed to create space 't3': PRIMARY KEY missing]]
        _, err = box.execute([[CREATE TABLE t3(a NUMBER);]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = [[Failed to create space 't4': PRIMARY KEY missing]]
        _, err = box.execute([[CREATE TABLE t4(a NUMBER, b TEXT);]])
        t.assert_equals(tostring(err), exp_err)

        exp_err = [[Failed to create space 't5': PRIMARY KEY missing]]
        _, err = box.execute([[CREATE TABLE t5(a NUMBER, b NUMBER UNIQUE);]])
        t.assert_equals(tostring(err), exp_err)

        box.execute([[DROP TABLE t1;]])
    end)
end

-- gh-3522: invalid primary key name
g.test_3522_invalid_primary_key_name = function(cg)
    cg.server:exec(function()
        local exp_err = [[Can't resolve field 'b']]
        local sql = [[CREATE TABLE tx (a INT, PRIMARY KEY (b));]]
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)
   end)
end

-- Make sure that 'pragma table_info()' correctly handles tables
-- without primary key.
g.test_4745_table_info_assertion = function(cg)
    cg.server:exec(function(engine)
        local T = box.schema.create_space('T', {
            engine = engine,
            format = {{'i', 'integer'}}
        })
        local _, err = box.execute([[pragma table_info(T);]])
        t.assert_equals(err, nil)
        T:drop()
    end, {cg.params.engine})
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
