#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(80)

-- Check that SCALAR and NUMBER meta-types works as intended.
box.execute([[CREATE TABLE t (i INT PRIMARY KEY, s SCALAR, n NUMBER, a ANY);]])
box.execute([[CREATE TABLE t1 (id INTEGER PRIMARY KEY AUTOINCREMENT, a ANY,
                               g UNSIGNED, t STRING, n NUMBER, f DOUBLE,
                               i INTEGER, b BOOLEAN, v VARBINARY, s SCALAR,
                               d DECIMAL, u UUID);]])
box.execute([[INSERT INTO t1(id) VALUES(NULL);]])

--
-- Check that implicit cast from numeric types to NUMBER and from scalar types
-- to SCALAR works properly.
--
local uuid_str = [[11111111-1111-1111-1111-111111111111]]
local uuid = require('uuid').fromstr(uuid_str)
local dec = require('decimal').new(1.5)
local bin = require('varbinary').new("5")
test:do_execsql_test(
    "metatypes-1.1",
    [[
        INSERT INTO t VALUES(1, 1, 1, 1);
        INSERT INTO t VALUES(2, 2e0, 2e0, 2e0);
        INSERT INTO t(i, s) VALUES(3, '3');
        INSERT INTO t(i, s) VALUES(4, true);
        INSERT INTO t(i, s) VALUES(5, x'35');
        INSERT INTO t(i, s) VALUES(6, CAST(']]..uuid_str..[[' AS UUID));
        INSERT INTO t(i, a) VALUES(7, '3');
        INSERT INTO t(i, a) VALUES(8, true);
        INSERT INTO t(i, a) VALUES(9, x'35');
        INSERT INTO t(i, a) VALUES(10, CAST(']]..uuid_str..[[' AS UUID));
        INSERT INTO t(i, a) VALUES(11, 3e0);
        INSERT INTO t(i, a) VALUES(12, CAST(1.5 AS DECIMAL));
        SELECT * FROM t;
    ]], {
        1, 1, 1, 1,
        2, 2, 2, 2,
        3, "3", "", "",
        4, true, "", "",
        5, bin, "", "",
        6, uuid, "", "",
        7, "", "", "3",
        8, "", "", true,
        9, "", "", bin,
        10, "", "", uuid,
        11, "", "", 3,
        12, "", "", dec
    })

-- Check that typeof() returns right result.
test:do_execsql_test(
    "metatypes-1.2",
    [[
        SELECT typeof(s) FROM t;
    ]], {
        "scalar", "scalar", "scalar", "scalar", "scalar", "scalar",
        "NULL", "NULL", "NULL", "NULL", "NULL", "NULL"
    })

test:do_execsql_test(
    "metatypes-1.3",
    [[
        SELECT typeof(n) FROM t;
    ]], {
        "number", "number", "NULL", "NULL", "NULL", "NULL", "NULL", "NULL",
        "NULL", "NULL", "NULL", "NULL"
    })

test:do_execsql_test(
    "metatypes-1.4",
    [[
        SELECT typeof(a) FROM t;
    ]], {
        "any", "any", "NULL", "NULL", "NULL", "NULL", "any", "any", "any",
        "any", "any", "any"
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

-- Check that implicit cast from ANY to any other type is prohibited.
test:do_execsql_test(
    "metatypes-2.6",
    [[
        INSERT INTO t1(a) VALUES(CAST(1 AS ANY));
        SELECT * FROM t1;
    ]], {
        1, "", "", "", "", "", "", "", "", "", "", "",
        2, 1, "", "", "", "", "", "", "", "", "", ""
    })

test:do_catchsql_test(
    "metatypes-2.7",
    [[
        INSERT INTO t1(g) VALUES(CAST(1 AS ANY));
    ]], {
        1, "Type mismatch: can not convert any(1) to unsigned"
    })

test:do_catchsql_test(
    "metatypes-2.8",
    [[
        INSERT INTO t1(t) VALUES(CAST(1 AS ANY));
    ]], {
        1, "Type mismatch: can not convert any(1) to string"
    })

test:do_catchsql_test(
    "metatypes-2.9",
    [[
        INSERT INTO t1(n) VALUES(CAST(1 AS ANY));
    ]], {
        1, "Type mismatch: can not convert any(1) to number"
    })

test:do_catchsql_test(
    "metatypes-2.10",
    [[
        INSERT INTO t1(f) VALUES(CAST(1 AS ANY));
    ]], {
        1, "Type mismatch: can not convert any(1) to double"
    })

test:do_catchsql_test(
    "metatypes-2.11",
    [[
        INSERT INTO t1(i) VALUES(CAST(1 AS ANY));
    ]], {
        1, "Type mismatch: can not convert any(1) to integer"
    })

test:do_catchsql_test(
    "metatypes-2.12",
    [[
        INSERT INTO t1(b) VALUES(CAST(1 AS ANY));
    ]], {
        1, "Type mismatch: can not convert any(1) to boolean"
    })

test:do_catchsql_test(
    "metatypes-2.13",
    [[
        INSERT INTO t1(v) VALUES(CAST(1 AS ANY));
    ]], {
        1, "Type mismatch: can not convert any(1) to varbinary"
    })

test:do_catchsql_test(
    "metatypes-2.14",
    [[
        INSERT INTO t1(s) VALUES(CAST(1 AS ANY));
    ]], {
        1, "Type mismatch: can not convert any(1) to scalar"
    })

test:do_catchsql_test(
    "metatypes-2.15",
    [[
        INSERT INTO t1(d) VALUES(CAST(1 AS ANY));
    ]], {
        1, "Type mismatch: can not convert any(1) to decimal"
    })

test:do_catchsql_test(
    "metatypes-2.16",
    [[
        INSERT INTO t1(u) VALUES(CAST(1 AS ANY));
    ]], {
        1, "Type mismatch: can not convert any(1) to uuid"
    })

-- Check that arithmetic operations are prohibited for NUMBER, SCALAR and ANY.
test:do_catchsql_test(
    "metatypes-3.1",
    [[
        SELECT 1 + CAST(1 AS NUMBER);
    ]], {
        1, "Type mismatch: can not convert number(1) to integer, decimal or double"
    })

test:do_catchsql_test(
    "metatypes-3.2",
    [[
        SELECT CAST(1 AS SCALAR) * 1;
    ]], {
        1, "Type mismatch: can not convert scalar(1) to integer, decimal or double"
    })

test:do_catchsql_test(
    "metatypes-3.3",
    [[
        SELECT CAST(1 AS ANY) - 1;
    ]], {
        1, "Type mismatch: can not convert any(1) to integer, decimal, "..
           "double, datetime or interval"
    })

-- Check that bitwise operations are prohibited for NUMBER, SCALAR and ANY.
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

test:do_catchsql_test(
    "metatypes-4.3",
    [[
        SELECT CAST(1 AS ANY) | 1;
    ]], {
        1, "Type mismatch: can not convert any(1) to unsigned"
    })

-- Check that concatination is prohibited for SCALAR and ANY.
test:do_catchsql_test(
    "metatypes-5",
    [[
        SELECT CAST('asd' AS SCALAR) || 'dsa';
    ]], {
        1, "Inconsistent types: expected string or varbinary got scalar('asd')"
    })

test:do_catchsql_test(
    "metatypes-5",
    [[
        SELECT CAST('asd' AS ANY) || 'dsa';
    ]], {
        1, "Inconsistent types: expected string or varbinary got any('asd')"
    })

-- Check that SCALAR values can be compared to values of any other scalar type.
test:do_execsql_test(
    "metatypes-6.1",
    [[
        SELECT s > false FROM t;
    ]], {
        true, true, true, true, true, true, "", "", "", "", "", ""
    })

test:do_execsql_test(
    "metatypes-6.2",
    [[
        SELECT s = 1 FROM t;
    ]], {
        true, false, false, false, false, false, "", "", "", "", "", ""
    })

test:do_execsql_test(
    "metatypes-6.3",
    [[
        SELECT s != 1.5 FROM t;
    ]], {
        true, true, true, true, true, true, "", "", "", "", "", ""
    })

test:do_execsql_test(
    "metatypes-6.4",
    [[
        SELECT s <= 'abc' FROM t;
    ]], {
        true, true, true, true, false, false, "", "", "", "", "", ""
    })

test:do_execsql_test(
    "metatypes-6.5",
    [[
        SELECT s < x'10' FROM t;
    ]], {
        true, true, true, true, false, false, "", "", "", "", "", ""
    })

test:do_execsql_test(
    "metatypes-6.6",
    [[
        SELECT s > CAST('11111111-1111-1111-1111-111111111110' AS UUID) FROM t;
    ]], {
        false, false, false, false, false, true, "", "", "", "", "", ""
    })

-- Check that ANY values cannot be compared to values of any other scalar type.
test:do_catchsql_test(
    "metatypes-7.1",
    [[
        SELECT a > false FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to comparable type"
    })

test:do_catchsql_test(
    "metatypes-7.2",
    [[
        SELECT a = 1 FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to comparable type"
    })

test:do_catchsql_test(
    "metatypes-7.3",
    [[
        SELECT a != 1.5 FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to comparable type"
    })

test:do_catchsql_test(
    "metatypes-7.4",
    [[
        SELECT a <= 'abc' FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to comparable type"
    })

test:do_catchsql_test(
    "metatypes-7.5",
    [[
        SELECT a < x'10' FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to comparable type"
    })

test:do_catchsql_test(
    "metatypes-7.6",
    [[
        SELECT a > CAST('11111111-1111-1111-1111-111111111110' AS UUID) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to comparable type"
    })

test:do_catchsql_test(
    "metatypes-7.7",
    [[
        SELECT a >= CAST(1 AS DECIMAL) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to comparable type"
    })

test:do_catchsql_test(
    "metatypes-7.8",
    [[
        SELECT a = CAST(1 AS NUMBER) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to comparable type"
    })

test:do_catchsql_test(
    "metatypes-7.9",
    [[
        SELECT a != CAST(1 AS SCALAR) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to comparable type"
    })

-- Make sure the SQL built-in functions work correctly with ANY.
test:do_catchsql_test(
    "metatypes-8.1",
    [[
        SELECT ABS(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to decimal"
    })

test:do_catchsql_test(
    "metatypes-8.2",
    [[
        SELECT AVG(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to decimal"
    })

test:do_catchsql_test(
    "metatypes-8.3",
    [[
        SELECT CHAR(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to integer"
    })

test:do_catchsql_test(
    "metatypes-8.4",
    [[
        SELECT CHARACTER_LENGTH(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to string"
    })

test:do_catchsql_test(
    "metatypes-8.5",
    [[
        SELECT CHAR_LENGTH(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to string"
    })

test:do_execsql_test(
    "metatypes-8.6",
    [[
        SELECT COALESCE(s, a) FROM t;
    ]], {
        1, 2, "3", true, bin, uuid, "3", true, bin, uuid, 3, dec
    })

test:do_execsql_test(
    "metatypes-8.7",
    [[
        SELECT COUNT(a) FROM t;
    ]], {
        8
    })

test:do_catchsql_test(
    "metatypes-8.8",
    [[
        SELECT GREATEST(s, a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to scalar"
    })

test:do_catchsql_test(
    "metatypes-8.9",
    [[
        SELECT GROUP_CONCAT(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to string"
    })

test:do_catchsql_test(
    "metatypes-8.10",
    [[
        SELECT HEX(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to varbinary"
    })

test:do_execsql_test(
    "metatypes-8.11",
    [[
        SELECT IFNULL(s, a) FROM t;
    ]], {
        1, 2, "3", true, bin, uuid, "3", true, bin, uuid, 3, dec
    })

test:do_catchsql_test(
    "metatypes-8.12",
    [[
        SELECT LEAST(s, a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to scalar"
    })

test:do_catchsql_test(
    "metatypes-8.13",
    [[
        SELECT LENGTH(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to string"
    })

test:do_catchsql_test(
    "metatypes-8.14",
    [[
        SELECT s LIKE a FROM t;
    ]], {
        1, [[Failed to execute SQL statement: wrong arguments for function ]]..
           [[LIKE()]]
    })

test:do_execsql_test(
    "metatypes-8.15",
    [[
        SELECT LIKELIHOOD(a, 0.5e0) FROM t;
    ]], {
        1, 2, "", "", "", "", "3", true, bin, uuid, 3, dec
    })

test:do_execsql_test(
    "metatypes-8.16",
    [[
        SELECT LIKELY(a) FROM t;
    ]], {
        1, 2, "", "", "", "", "3", true, bin, uuid, 3, dec
    })

test:do_catchsql_test(
    "metatypes-8.17",
    [[
        SELECT LOWER(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to string"
    })

test:do_catchsql_test(
    "metatypes-8.18",
    [[
        SELECT MAX(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to scalar"
    })

test:do_catchsql_test(
    "metatypes-8.19",
    [[
        SELECT MIN(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to scalar"
    })

test:do_catchsql_test(
    "metatypes-8.20",
    [[
        SELECT NULLIF(s, a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to scalar"
    })

test:do_catchsql_test(
    "metatypes-8.21",
    [[
        SELECT POSITION(s, a) FROM t;
    ]], {
        1, [[Failed to execute SQL statement: wrong arguments for function ]]..
           [[POSITION()]]
    })

test:do_execsql_test(
    "metatypes-8.22",
    [[
        SELECT PRINTF(a) FROM t;
    ]], {
        "1", "2.0", "", "", "", "", "3", "TRUE", "5",
        "11111111-1111-1111-1111-111111111111", "3.0", "1.5"
    })

test:do_execsql_test(
    "metatypes-8.23",
    [[
        SELECT QUOTE(a) FROM t;
    ]], {
        1, 2, "NULL", "NULL", "NULL", "NULL", "'3'", "TRUE", "X'35'",
        "11111111-1111-1111-1111-111111111111", 3, dec
    })

test:do_catchsql_test(
    "metatypes-8.24",
    [[
        SELECT RANDOMBLOB(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to integer"
    })

test:do_catchsql_test(
    "metatypes-8.25",
    [[
        SELECT REPLACE(s, n, a) FROM t;
    ]], {
        1, [[Failed to execute SQL statement: wrong arguments for function ]]..
           [[REPLACE()]]
    })

test:do_catchsql_test(
    "metatypes-8.26",
    [[
        SELECT ROUND(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to decimal"
    })

test:do_catchsql_test(
    "metatypes-8.27",
    [[
        SELECT SOUNDEX(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to string"
    })

test:do_catchsql_test(
    "metatypes-8.28",
    [[
        SELECT SUBSTR(a, 1, 1) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to string"
    })

test:do_catchsql_test(
    "metatypes-8.29",
    [[
        SELECT SUM(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to decimal"
    })

test:do_catchsql_test(
    "metatypes-8.30",
    [[
        SELECT TOTAL(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to decimal"
    })

test:do_catchsql_test(
    "metatypes-8.31",
    [[
        SELECT TRIM(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to string"
    })

test:do_execsql_test(
    "metatypes-8.32",
    [[
        SELECT TYPEOF(a) FROM t;
    ]], {
        "any", "any", "NULL", "NULL", "NULL", "NULL", "any", "any", "any",
        "any", "any", "any"
    })

test:do_catchsql_test(
    "metatypes-8.33",
    [[
        SELECT UNICODE(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to string"
    })

test:do_execsql_test(
    "metatypes-8.34",
    [[
        SELECT UNLIKELY(a) FROM t;
    ]], {
        1, 2, "", "", "", "", "3", true, bin, uuid, 3, dec
    })

test:do_catchsql_test(
    "metatypes-8.35",
    [[
        SELECT UPPER(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to string"
    })

test:do_catchsql_test(
    "metatypes-8.36",
    [[
        SELECT UUID(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to integer"
    })

test:do_catchsql_test(
    "metatypes-8.37",
    [[
        SELECT ZEROBLOB(a) FROM t;
    ]], {
        1, "Type mismatch: can not convert any(1) to integer"
    })


box.execute([[DROP TABLE t;]])

test:finish_test()
