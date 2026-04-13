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

--
-- This code checks that basic SQL query operations
-- do not work without a primary key
--
g.test_no_pk_space = function(cg)
    cg.server:exec(function()
        local format = {{'id', 'integer'}}
        local s = box.schema.create_space('test', {format = format})

        local exp_err = [[SQL does not support spaces without primary key]]
        local _, err = box.execute([[SELECT * FROM "test";]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[INSERT INTO "test" VALUES (1);]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[DELETE FROM "test";]])
        t.assert_equals(tostring(err), exp_err)

        _, err = box.execute([[UPDATE "test" SET id = 3;]])
        t.assert_equals(tostring(err), exp_err)

        s:drop()
    end)
end
