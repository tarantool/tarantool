#!/usr/bin/env tarantool
local build_path = os.getenv("BUILDDIR")
package.cpath = build_path..'/test/sql-tap/?.so;'..build_path..'/test/sql-tap/?.dylib;'..package.cpath

local test = require("sqltester")
test:plan(101)

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

-- Check that explicit cast from DECIMAL to another types works as intended.
test:do_execsql_test(
    "dec-10.1.1",
    [[
        SELECT cast(u AS UNSIGNED) FROM t2;
    ]], {
        111, 3333, 55555
    })

test:do_execsql_test(
    "dec-10.1.2",
    [[
        SELECT cast(u AS STRING) FROM t2;
    ]], {
        "111", "3333", "55555"
    })

test:do_execsql_test(
    "dec-10.1.3",
    [[
        SELECT cast(u AS NUMBER) FROM t2;
    ]], {
        dec1, dec3, dec2
    })

test:do_execsql_test(
    "dec-10.1.4",
    [[
        SELECT cast(u AS DOUBLE) FROM t2;
    ]], {
        111, 3333, 55555
    })

test:do_execsql_test(
    "dec-10.1.5",
    [[
        SELECT cast(u AS INTEGER) FROM t2;
    ]], {
        111, 3333, 55555
    })

test:do_catchsql_test(
    "dec-10.1.6",
    [[
        SELECT cast(u AS BOOLEAN) FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to boolean"
    })

test:do_catchsql_test(
    "dec-10.1.7",
    [[
        SELECT hex(cast(u AS VARBINARY)) FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to varbinary"
    })

test:do_execsql_test(
    "dec-10.1.8",
    [[
        SELECT cast(u AS SCALAR) FROM t2;
    ]], {
        dec1, dec3, dec2
    })

test:do_catchsql_test(
    "dec-10.1.9",
    [[
        SELECT cast(u AS UUID) FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to uuid"
    })

-- Check that explicit cast from another types to DECIMAL works as intended.
test:do_execsql_test(
    "dec-10.2.1",
    [[
        SELECT cast(111 AS DECIMAL);
    ]], {
        dec1
    })

test:do_catchsql_test(
    "dec-10.2.2",
    [[
        SELECT cast(x'1234567890abcdef' AS DECIMAL) FROM t2 LIMIT 1;
    ]], {
        1, "Type mismatch: can not convert varbinary(x'1234567890ABCDEF') to decimal"
    })

test:do_execsql_test(
    "dec-10.2.3",
    [[
        SELECT cast('111' AS DECIMAL);
    ]], {
        dec1
    })

test:do_execsql_test(
    "dec-10.2.4",
    [[
        SELECT cast(111.0 AS DECIMAL);
    ]], {
        dec1
    })

test:do_execsql_test(
    "dec-10.2.5",
    [[
        SELECT cast(-1 AS DECIMAL);
    ]], {
        dec.new(-1)
    })

test:do_catchsql_test(
    "dec-10.2.6",
    [[
        SELECT cast(true AS DECIMAL);
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to decimal"
    })

test:do_catchsql_test(
    "dec-10.2.7",
    [[
        SELECT cast(cast(x'11111111111111111111111111111111' AS UUID) AS DECIMAL);
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to decimal"
    })

test:execsql([[
    CREATE TABLE tu (id INT PRIMARY KEY AUTOINCREMENT, u UNSIGNED);
    CREATE TABLE ts (id INT PRIMARY KEY AUTOINCREMENT, s STRING);
    CREATE TABLE tn (id INT PRIMARY KEY AUTOINCREMENT, n NUMBER);
    CREATE TABLE td (id INT PRIMARY KEY AUTOINCREMENT, d DOUBLE);
    CREATE TABLE ti (id INT PRIMARY KEY AUTOINCREMENT, i INTEGER);
    CREATE TABLE tb (id INT PRIMARY KEY AUTOINCREMENT, b BOOLEAN);
    CREATE TABLE tv (id INT PRIMARY KEY AUTOINCREMENT, v VARBINARY);
    CREATE TABLE tsc (id INT PRIMARY KEY AUTOINCREMENT, sc SCALAR);
    CREATE TABLE tuu (id INT PRIMARY KEY AUTOINCREMENT, uu UUID);
    CREATE TABLE tsu (s STRING PRIMARY KEY, u UUID);
]])

-- Check that implcit cast from DECIMAL to another types works as intended.
test:do_execsql_test(
    "dec-11.1.1",
    [[
        INSERT INTO tu(u) SELECT u FROM t2;
        SELECT * FROM tu;
    ]], {
        1, 111, 2, 3333, 3, 55555
    })

test:do_catchsql_test(
    "dec-11.1.2",
    [[
        INSERT INTO ts(s) SELECT u FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to string"
    })

test:do_execsql_test(
    "dec-11.1.3",
    [[
        INSERT INTO tn(n) SELECT u FROM t2;
        SELECT * FROM tn;
    ]], {
        1, dec1, 2, dec3, 3, dec2
    })

test:do_execsql_test(
    "dec-11.1.4",
    [[
        INSERT INTO td(d) SELECT u FROM t2;
        SELECT * FROM td;
    ]], {
        1, 111, 2, 3333, 3, 55555
    })

test:do_execsql_test(
    "dec-11.1.5",
    [[
        INSERT INTO ti(i) SELECT u FROM t2;
        SELECT * FROM ti;
    ]], {
        1, 111, 2, 3333, 3, 55555
    })

test:do_catchsql_test(
    "dec-11.1.6",
    [[
        INSERT INTO tb(b) SELECT u FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to boolean"
    })

test:do_catchsql_test(
    "dec-11.1.7",
    [[
        INSERT INTO tv(v) SELECT u FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to varbinary"
    })

test:do_execsql_test(
    "dec-11.1.8",
    [[
        INSERT INTO tsc(sc) SELECT u FROM t2;
        SELECT * FROM tsc;
    ]], {
        1, dec1, 2, dec3, 3, dec2
    })

test:do_catchsql_test(
    "dec-11.1.9",
    [[
        INSERT INTO tuu(uu) SELECT u FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to uuid"
    })

-- Check that implicit cast from another types to DECIMAL works as intended.
test:do_catchsql_test(
    "dec-11.2.1",
    [[
        INSERT INTO tsu VALUES ('1_unsigned', 1);
    ]], {
        1, "Type mismatch: can not convert integer(1) to uuid"
    })

test:do_catchsql_test(
    "dec-11.2.2",
    [[
        INSERT INTO tsu VALUES ('2_string_right', '11111111-1111-1111-1111-111111111111');
    ]], {
        1, "Type mismatch: can not convert string('11111111-1111-1111-1111-111111111111') to uuid"
    })

test:do_catchsql_test(
    "dec-11.2.3",
    [[
        INSERT INTO tsu VALUES ('3_string_wrong', '1');
    ]], {
        1, "Type mismatch: can not convert string('1') to uuid"
    })

test:do_catchsql_test(
    "dec-11.2.4",
    [[
        INSERT INTO tsu VALUES ('4_double', 1.5);
    ]], {
        1, "Type mismatch: can not convert double(1.5) to uuid"
    })

test:do_catchsql_test(
    "dec-11.2.5",
    [[
        INSERT INTO tsu VALUES ('5_integer', -1);
    ]], {
        1, "Type mismatch: can not convert integer(-1) to uuid"
    })

test:do_catchsql_test(
    "dec-11.2.6",
    [[
        INSERT INTO tsu VALUES ('6_boolean', true);
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to uuid"
    })

test:do_catchsql_test(
    "dec-11.2.7",
    [[
        INSERT INTO tsu SELECT '7_varbinary', x'11111111111111111111111111111111' FROM t2 LIMIT 1;
    ]], {
        1, "Type mismatch: can not convert varbinary(x'11111111111111111111111111111111') to uuid"
    })

test:do_catchsql_test(
    "dec-11.2.8",
    [[
        INSERT INTO tsu VALUES ('8_varbinary', x'1234567890abcdef');
    ]], {
        1, "Type mismatch: can not convert varbinary(x'1234567890ABCDEF') to uuid"
    })

-- Check that LIMIT accepts DECIMAL as argument.
test:do_execsql_test(
    "dec-12.1",
    [[
        SELECT 1 LIMIT (SELECT u FROM t1 LIMIT 1);
    ]], {
        1
    })

-- Check that OFFSET accepts DECIMAL as argument.
test:do_execsql_test(
    "dec-12.2",
    [[
        SELECT 1 LIMIT 1 OFFSET (SELECT u FROM t1 LIMIT 1);
    ]], {
    })

-- Check that other numeric values could be used to search in DECIMAL index.
test:do_execsql_test(
    "dec-13.1.1",
    [[
        SELECT * FROM t2 WHERE u > 123;
    ]], {
        dec3, dec2
    })

test:do_execsql_test(
    "dec-13.1.2",
    [[
        SELECT * FROM t2 WHERE u < 123.5;
    ]], {
        dec1
    })

test:execsql([[
    CREATE TABLE t13i (i INTEGER PRIMARY KEY);
    CREATE TABLE t13u (u UNSIGNED PRIMARY KEY);
    CREATE TABLE t13d (d DOUBLE PRIMARY KEY);
    CREATE TABLE t13n (n NUMBER PRIMARY KEY);
    INSERT INTO t13i VALUES (1), (1000);
    INSERT INTO t13u VALUES (1), (1000);
    INSERT INTO t13d VALUES (1), (1000);
    INSERT INTO t13n VALUES (1), (1000);
]])

-- Check that DECIMAL values could be used to search in other numeric indexes.
test:do_execsql_test(
    "dec-13.2.1",
    [[
        SELECT * FROM t13i WHERE CAST(111 AS DECIMAL) > i;
    ]], {
        1
    })

test:do_execsql_test(
    "dec-13.2.2",
    [[
        SELECT * FROM t13u WHERE CAST(111 AS DECIMAL) < u;
    ]], {
        1000
    })

test:do_execsql_test(
    "dec-13.2.3",
    [[
        SELECT * FROM t13d WHERE CAST(111 AS DECIMAL) > d;
    ]], {
        1
    })

test:do_execsql_test(
    "dec-13.2.4",
    [[
        SELECT * FROM t13n WHERE CAST(111 AS DECIMAL) < n;
    ]], {
        1000
    })

-- Check that arithmetic operations work with UUIDs as intended.
test:do_execsql_test(
    "dec-14.1.1",
    [[
        SELECT -u FROM t2;
    ]], {
        dec.new(-111), dec.new(-3333), dec.new(-55555)
    })

test:do_execsql_test(
    "dec-14.1.2",
    [[
        SELECT u + 0 FROM t2;
    ]], {
        dec1, dec3, dec2
    })

test:do_execsql_test(
    "dec-14.1.3",
    [[
        SELECT u - 0.5 FROM t2;
    ]], {
        110.5, 3332.5, 55554.5
    })

test:do_execsql_test(
    "dec-14.1.4",
    [[
        SELECT u * 1 FROM t2;
    ]], {
        dec1, dec3, dec2
    })

test:do_execsql_test(
    "dec-14.1.5",
    [[
        SELECT u / 1.0 FROM t2;
    ]], {
        111, 3333, 55555
    })

test:do_catchsql_test(
    "dec-14.1.6",
    [[
        SELECT u % 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to integer"
    })

-- Check that bitwise operations work with UUIDs as intended.
test:do_catchsql_test(
    "dec-14.2.1",
    [[
        SELECT ~u FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to unsigned"
    })

test:do_catchsql_test(
    "dec-14.2.2",
    [[
        SELECT u >> 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to unsigned"
    })

test:do_catchsql_test(
    "dec-14.2.3",
    [[
        SELECT u << 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to unsigned"
    })

test:do_catchsql_test(
    "dec-14.2.4",
    [[
        SELECT u | 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to unsigned"
    })

test:do_catchsql_test(
    "dec-14.2.5",
    [[
        SELECT u & 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to unsigned"
    })

-- Check that logical operations work with UUIDs as intended.
test:do_catchsql_test(
    "dec-14.3.1",
    [[
        SELECT NOT u FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to boolean"
    })

test:do_catchsql_test(
    "dec-14.3.2",
    [[
        SELECT u AND true FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to boolean"
    })

test:do_catchsql_test(
    "dec-14.3.3",
    [[
        SELECT u OR true FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to boolean"
    })

test:do_catchsql_test(
    "dec-14.3.4",
    [[
        SELECT true AND u FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to boolean"
    })

test:do_catchsql_test(
    "dec-14.3.5",
    [[
        SELECT true OR u FROM t2;
    ]], {
        1, "Type mismatch: can not convert decimal(111) to boolean"
    })

test:do_catchsql_test(
    "dec-15",
    [[
        SELECT u || u from t2;
    ]], {
        1, "Inconsistent types: expected string or varbinary got decimal(111)"
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
