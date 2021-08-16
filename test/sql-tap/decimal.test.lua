#!/usr/bin/env tarantool
local build_path = os.getenv("BUILDDIR")
package.cpath = build_path..'/test/sql-tap/?.so;'..build_path..'/test/sql-tap/?.dylib;'..package.cpath

local test = require("sqltester")
test:plan(43)

local dec = require("decimal")
local dec1 = dec.new("111")
local dec2 = dec.new("55555")
local dec3 = dec.new("3333")
local dec4 = dec.new("-13")
local dec5 = dec.new("0")
local dec6 = dec.new("-0")

-- Check that it is possible to create spaces with DECIMAL field.
test:do_execsql_test(
    "dec-1",
    [[
        CREATE TABLE t0 (i INT PRIMARY KEY, u DEC);
        CREATE TABLE t1 (i INT PRIMARY KEY, u DEC);
        CREATE TABLE t2 (u DECIMAL PRIMARY KEY);
    ]], {
    })

box.space.T0:insert({1, dec1})
box.space.T0:insert({2, dec2})
box.space.T0:insert({3, dec3})
box.space.T0:insert({4, dec4})
box.space.T0:insert({5, dec5})
box.space.T0:insert({6, dec6})
box.space.T1:insert({1, dec1})
box.space.T1:insert({2, dec2})
box.space.T1:insert({3, dec3})
box.space.T1:insert({4, dec1})
box.space.T1:insert({5, dec1})
box.space.T1:insert({6, dec2})
box.space.T2:insert({dec1})
box.space.T2:insert({dec2})
box.space.T2:insert({dec3})

-- Check that SELECT can work with DECIMAL.
test:do_execsql_test(
    "dec-2.1.1",
    [[
        SELECT * FROM t0;
    ]], {
        1, dec1, 2, dec2, 3, dec3, 4, dec4, 5, dec5, 6, dec6
    })

test:do_execsql_test(
    "dec-2.1.2",
    [[
        SELECT * FROM t2;
    ]], {
        dec1, dec3, dec2
    })

-- Check that ORDER BY can work with DECIMAL.
test:do_execsql_test(
    "dec-2.2.1",
    [[
        SELECT * FROM t0 ORDER BY u;
    ]], {
        4, dec4, 5, dec5, 6, dec6, 1, dec1, 3, dec3, 2, dec2
    })

test:do_execsql_test(
    "dec-2.2.2",
    [[
        SELECT * FROM t0 ORDER BY u DESC;
    ]], {
        2, dec2, 3, dec3, 1, dec1, 5, dec5, 6, dec6, 4, dec4
    })

test:do_execsql_test(
    "dec-2.2.3",
    [[
        SELECT * FROM t2 ORDER BY u;
    ]], {
        dec1, dec3, dec2
    })

test:do_execsql_test(
    "dec-2.2.4",
    [[
        SELECT * FROM t2 ORDER BY u DESC;
    ]], {
        dec2, dec3, dec1
    })

-- Check that GROUP BY can work with DECIMAL.
test:do_execsql_test(
    "dec-2.3.1",
    [[
        SELECT count(*), u FROM t1 GROUP BY u;
    ]], {
        3, dec1, 1, dec3, 2, dec2
    })

test:do_execsql_test(
    "dec-2.3.2",
    [[
        SELECT count(*), u FROM t2 GROUP BY u;
    ]], {
        1, dec1, 1, dec3, 1, dec2
    })

-- Check that subselects can work with DECIMAL.
test:do_execsql_test(
    "dec-2.4",
    [[
        SELECT * FROM (SELECT * FROM (SELECT * FROM t2 LIMIT 2) LIMIT 2 OFFSET 1);
    ]], {
        dec3
    })

-- Check that DISTINCT can work with DECIMAL.
test:do_execsql_test(
    "dec-2.5",
    [[
        SELECT DISTINCT u FROM t1;
    ]], {
        dec1, dec2, dec3
    })

-- Check that VIEW can work with DECIMAL.
test:do_execsql_test(
    "dec-2.6",
    [[
        CREATE VIEW v AS SELECT u FROM t1;
        SELECT * FROM v;
    ]], {
        dec1, dec2, dec3, dec1, dec1, dec2
    })

test:execsql([[
    CREATE TABLE t3 (s SCALAR PRIMARY KEY);
]])

-- Check that SCALAR field can contain DECIMAL and use it in index.
test:do_execsql_test(
    "dec-3",
    [[
        INSERT INTO t3 SELECT u FROM t2;
        SELECT s, typeof(s) FROM t3;
    ]], {
        dec1, "scalar", dec3, "scalar", dec2, "scalar"
    })

-- Check that ephemeral space can work with DECIMAL.
test:do_execsql_test(
    "dec-4",
    [[
        EXPLAIN SELECT * from (VALUES(1)), t2;
    ]], {
        "/OpenTEphemeral/"
    })

test:execsql([[
    CREATE TABLE t5f (u DECIMAL PRIMARY KEY, f DECIMAL REFERENCES t5f(u));
    CREATE TABLE t5c (i INT PRIMARY KEY, f DECIMAL,
                      CONSTRAINT ck CHECK(f != 111));
    CREATE TABLE t5u (i INT PRIMARY KEY, f DECIMAL UNIQUE);
]])

-- Check that FOREIGN KEY constraint can work with DECIMAL.
test:do_catchsql_test(
    "dec-5.1.1",
    [[
        INSERT INTO t5f SELECT (SELECT u from t2 LIMIT 1 OFFSET 1), (SELECT u from t2 LIMIT 1);
    ]], {
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
    })

test:do_execsql_test(
    "dec-5.1.2",
    [[
        INSERT INTO t5f SELECT u, u from t2 LIMIT 1;
        SELECT * from t5f;
    ]], {
        dec1, dec1
    })

test:do_execsql_test(
    "dec-5.1.3",
    [[
        INSERT INTO t5f SELECT (SELECT u from t2 LIMIT 1 OFFSET 1), (SELECT u from t2 LIMIT 1);
        SELECT * from t5f;
    ]], {
        dec1, dec1, dec3, dec1
    })

-- Check that CHECK constraint can work with DECIMAL.
test:do_catchsql_test(
    "dec-5.2.1",
    [[
        INSERT INTO t5c SELECT 1, u FROM t2 LIMIT 1;
    ]], {
        1, "Check constraint failed 'CK': f != 111"
    })

test:do_execsql_test(
    "dec-5.2.2",
    [[
        INSERT INTO t5c SELECT 2, u FROM t2 LIMIT 1 OFFSET 1;
        SELECT * from t5c;
    ]], {
        2, dec3
    })

-- Check that UNIQUE constraint can work with DECIMAL.
test:do_execsql_test(
    "dec-5.3.1",
    [[
        INSERT INTO t5u SELECT 1, u FROM t2 LIMIT 1;
        SELECT * from t5u;
    ]], {
        1, dec1
    })

test:do_catchsql_test(
    "dec-5.3.2",
    [[
        INSERT INTO t5u SELECT 2, u FROM t2 LIMIT 1;
    ]], {
        1, 'Duplicate key exists in unique index "unique_unnamed_T5U_2" in '..
           'space "T5U" with old tuple - [1, 111] and new tuple - [2, 111]'
    })

local func = {language = 'Lua', body = 'function(x) return type(x) end',
              returns = 'string', param_list = {'any'}, exports = {'SQL'}}
box.schema.func.create('RETURN_TYPE', func);

-- Check that Lua user-defined functions can accept DECIMAL.
test:do_execsql_test(
    "dec-6.1",
    [[
        SELECT RETURN_TYPE(u) FROM t2;
    ]], {
        "cdata", "cdata", "cdata"
    })

func = {language = 'Lua', returns = 'decimal', param_list = {}, exports = {'SQL'},
        body = 'function(x) return require("decimal").new("111") end'}
box.schema.func.create('GET_DEC', func);

-- Check that Lua user-defined functions can return DECIMAL.
test:do_execsql_test(
    "dec-6.2",
    [[
        SELECT GET_DEC();
    ]], {
        dec1
    })

func = {language = 'C', returns = 'boolean', param_list = {'any'}, exports = {'SQL'}}
box.schema.func.create("decimal.is_dec", func)

-- Check that C user-defined functions can accept DECIMAL.
test:do_execsql_test(
    "dec-6.3",
    [[
        SELECT "decimal.is_dec"(i), "decimal.is_dec"(u) FROM t1 LIMIT 1;
    ]], {
        false, true
    })

func = {language = 'C', returns = 'decimal', param_list = {}, exports = {'SQL'}}
box.schema.func.create("decimal.ret_dec", func)

-- Check that C user-defined functions can return DECIMAL.
test:do_execsql_test(
    "dec-6.4",
    [[
        SELECT "decimal.ret_dec"();
    ]], {
        dec1
    })

test:execsql([[
    CREATE TABLE t7 (i INT PRIMARY KEY AUTOINCREMENT, u DECIMAL);
    CREATE TABLE t7t (u DECIMAL PRIMARY KEY);
    CREATE TRIGGER t AFTER INSERT ON t7 FOR EACH ROW BEGIN INSERT INTO t7t SELECT new.u; END;
]])

-- Check that trigger can work with DECIMAL.
test:do_execsql_test(
    "dec-7",
    [[
        INSERT INTO t7(u) SELECT * FROM t2;
        SELECT * FROM t7t;
    ]], {
        dec1, dec3, dec2
    })

-- Check that JOIN by DECIMAL field works.
test:do_execsql_test(
    "dec-8.1",
    [[
        SELECT * FROM t1 JOIN t2 on t1.u = t2.u;
    ]], {
        1, dec1, dec1, 2, dec2, dec2, 3, dec3, dec3,
        4, dec1, dec1, 5, dec1, dec1, 6, dec2, dec2
    })

test:do_execsql_test(
    "dec-8.2",
    [[
        SELECT * FROM t1 LEFT JOIN t2 on t1.u = t2.u;
    ]], {
        1, dec1, dec1, 2, dec2, dec2, 3, dec3, dec3,
        4, dec1, dec1, 5, dec1, dec1, 6, dec2, dec2
    })

test:do_execsql_test(
    "dec-8.3",
    [[
        SELECT * FROM t1 INNER JOIN t2 on t1.u = t2.u;
    ]], {
        1, dec1, dec1, 2, dec2, dec2, 3, dec3, dec3,
        4, dec1, dec1, 5, dec1, dec1, 6, dec2, dec2
    })

-- Check that comparison with DECIMAL works as intended.
test:do_execsql_test(
    "dec-9.1.1",
    [[
        SELECT u > 1 FROM t2;
    ]], {
        true, true, true
    })

test:do_catchsql_test(
    "dec-9.1.2",
    [[
        SELECT u > CAST('11111111-1111-1111-1111-111111111111' AS UUID) FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to number"
    })

test:do_catchsql_test(
    "dec-9.1.3",
    [[
        SELECT u > '1' FROM t2;
    ]], {
        1, "Type mismatch: can not convert string('1') to number"
    })

test:do_execsql_test(
    "dec-9.1.4",
    [[
        SELECT u > 1.5 FROM t2;
    ]], {
        true, true, true
    })

test:do_execsql_test(
    "dec-9.1.5",
    [[
        SELECT u > -1 FROM t2;
    ]], {
        true, true, true
    })

test:do_catchsql_test(
    "dec-9.1.6",
    [[
        SELECT u > true FROM t2;
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to number"
    })

test:do_catchsql_test(
    "dec-9.1.7",
    [[
        SELECT u > x'31' FROM t2;
    ]], {
        1, "Type mismatch: can not convert varbinary(x'31') to number"
    })

test:do_execsql_test(
    "dec-9.2.1",
    [[
        SELECT u = 1 FROM t2;
    ]], {
        false, false, false
    })

test:do_catchsql_test(
    "dec-9.2.2",
    [[
        SELECT u = CAST('11111111-1111-1111-1111-111111111111' AS UUID) FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to number"
    })

test:do_catchsql_test(
    "dec-9.2.3",
    [[
        SELECT u = '1' FROM t2;
    ]], {
        1, "Type mismatch: can not convert string('1') to number"
    })

test:do_execsql_test(
    "dec-9.2.4",
    [[
        SELECT u = 1.5 FROM t2;
    ]], {
        false, false, false
    })

test:do_execsql_test(
    "dec-9.2.5",
    [[
        SELECT u = -1 FROM t2;
    ]], {
        false, false, false
    })

test:do_catchsql_test(
    "dec-9.2.6",
    [[
        SELECT u = true FROM t2;
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to number"
    })

test:do_catchsql_test(
    "dec-9.2.7",
    [[
        SELECT u = x'31' FROM t2;
    ]], {
        1, "Type mismatch: can not convert varbinary(x'31') to number"
    })

test:execsql([[
    DROP TRIGGER t;
    DROP VIEW v;
    DROP TABLE t7t;
    DROP TABLE t7;
    DROP TABLE t5u;
    DROP TABLE t5c;
    DROP TABLE t5f;
    DROP TABLE t3;
    DROP TABLE t2;
    DROP TABLE t1;
]])

test:finish_test()
