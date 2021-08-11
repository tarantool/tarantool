#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(13)

-- Check that SCALAR and NUMBER meta-types works as intended.
box.execute([[CREATE TABLE t (i INT PRIMARY KEY, s SCALAR, n NUMBER);]])

--
-- Check that implicit cast from numeric types to NUMBER and from scalar types
-- to SCALAR works properly.
--
local uuid = [[CAST('11111111-1111-1111-1111-111111111111' AS UUID)]]
test:do_execsql_test(
    "metatypes-1.1",
    [[
        INSERT INTO t VALUES(1, 1, 1);
        INSERT INTO t VALUES(2, 2.0, 2.0);
        INSERT INTO t(i, s) VALUES(3, '3');
        INSERT INTO t(i, s) VALUES(4, true);
        INSERT INTO t(i, s) VALUES(5, x'35');
        INSERT INTO t(i, s) VALUES(6, ]]..uuid..[[);
        SELECT * FROM t;
    ]], {
        1,1,1,
        2,2,2,
        3,"3","",
        4,true,"",
        5,"5","",
        6,require('uuid').fromstr('11111111-1111-1111-1111-111111111111'),""
    })

-- Check that typeof() returns right result.
test:do_execsql_test(
    "metatypes-1.2",
    [[
        SELECT typeof(s) FROM t;
    ]], {
        "scalar","scalar","scalar","scalar","scalar","scalar"
    })

test:do_execsql_test(
    "metatypes-1.3",
    [[
        SELECT typeof(n) FROM t;
    ]], {
        "number","number","NULL","NULL","NULL","NULL"
    })

--
-- Check that implicit cast from NUMBER to numeric types and from SCALAR to
-- scalar types is prohibited.
--
test:do_catchsql_test(
    "metatypes-2.1",
    [[
        INSERT INTO t(i) VALUES(CAST(7 AS SCALAR));
    ]], {
        1, "Type mismatch: can not convert scalar(7) to integer"
    })

test:do_catchsql_test(
    "metatypes-2.2",
    [[
        INSERT INTO t(i, n) VALUES(8, CAST(1.5 AS SCALAR));
    ]], {
        1, "Type mismatch: can not convert scalar(1.5) to number"
    })

test:do_catchsql_test(
    "metatypes-2.3",
    [[
        INSERT INTO t(i) VALUES(CAST(9 AS NUMBER));
    ]], {
        1, "Type mismatch: can not convert number(9) to integer"
    })

test:do_catchsql_test(
    "metatypes-2.4",
    [[
        UPDATE t SET i = CAST(10 AS SCALAR);
    ]], {
        1, "Type mismatch: can not convert scalar(10) to integer"
    })

test:do_catchsql_test(
    "metatypes-2.5",
    [[
        UPDATE t SET i = CAST(11 AS NUMBER);
    ]], {
        1, "Type mismatch: can not convert number(11) to integer"
    })

-- Check that arithmetic operations are prohibited for NUMBER and SCALAR values.
test:do_catchsql_test(
    "metatypes-3.1",
    [[
        SELECT 1 + CAST(1 AS NUMBER);
    ]], {
        1, "Type mismatch: can not convert number(1) to integer, unsigned or double"
    })

test:do_catchsql_test(
    "metatypes-3.2",
    [[
        SELECT CAST(1 AS SCALAR) * 1;
    ]], {
        1, "Type mismatch: can not convert scalar(1) to integer, unsigned or double"
    })

-- Check that bitwise operations are prohibited for NUMBER and SCALAR values.
test:do_catchsql_test(
    "metatypes-4.1",
    [[
        SELECT 1 & CAST(1 AS NUMBER);
    ]], {
        1, "Type mismatch: can not convert number(1) to unsigned"
    })

test:do_catchsql_test(
    "metatypes-4.2",
    [[
        SELECT CAST(1 AS SCALAR) >> 1;
    ]], {
        1, "Type mismatch: can not convert scalar(1) to unsigned"
    })

-- Check that concatination is prohibited for SCALAR values.
test:do_catchsql_test(
    "metatypes-5",
    [[
        SELECT CAST('asd' AS SCALAR) || 'dsa';
    ]], {
        1, "Inconsistent types: expected string or varbinary got scalar('asd')"
    })

box.execute([[DROP TABLE t;]])

test:finish_test()
