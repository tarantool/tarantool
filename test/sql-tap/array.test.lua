#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(117)

box.schema.func.create('A1', {
    language = 'Lua',
    body = 'function(a) return {a} end',
    returns = 'array',
    param_list = {'any'},
    exports = {'LUA', 'SQL'}
});

box.schema.func.create('A2', {
    language = 'Lua',
    body = 'function(a, b) return {a, b} end',
    returns = 'array',
    param_list = {'any', 'any'},
    exports = {'LUA', 'SQL'}
});

box.schema.func.create('A3', {
    language = 'Lua',
    body = 'function(a, b, c) return {a, b, c} end',
    returns = 'array',
    param_list = {'any', 'any', 'any'},
    exports = {'LUA', 'SQL'}
});

-- Make sure it is possible to create tables with field type ARRAY.
test:do_execsql_test(
    "array-1",
    [[
        CREATE TABLE t (i INT PRIMARY KEY AUTOINCREMENT, a ARRAY);
    ]], {
    })

box.space.T:insert({0, {1, 2, 3, 4}})

-- Make sure it is possible to select from ARRAY field.
test:do_execsql_test(
    "array-2",
    [[
        SELECT i, a FROM t;
    ]], {
        0, {1, 2, 3, 4},
    })

-- Make sure it is possible to insert into ARRAY field.
test:do_execsql_test(
    "array-3",
    [[
        INSERT INTO t(a) VALUES(NULL);
        INSERT INTO t(a) VALUES(a1(1));
        INSERT INTO t(a) VALUES(a2(2, 3));
        INSERT INTO t(a) VALUES(a3(4, 5, 6));
        SELECT i, a FROM t;
    ]], {
        0, {1, 2, 3, 4},
        1, "",
        2, {1},
        3, {2, 3},
        4, {4, 5, 6},
    })

-- Make sure it is possible to delete from ARRAY field.
test:do_execsql_test(
    "array-4",
    [[
        DELETE FROM t WHERE i < 3;
        SELECT i, a FROM t;
    ]], {
        3, {2, 3},
        4, {4, 5, 6},
    })

-- Make sure it is possible to update ARRAY field.
test:do_execsql_test(
    "array-5",
    [[
        UPDATE t SET a = a1(123) WHERE i = 3;
        SELECT i, a FROM t;
    ]], {
        3, {123},
        4, {4, 5, 6},
    })

-- Make sure ARRAY can only be explicitly cast to ANY.
test:do_execsql_test(
    "array-6.1",
    [[
        SELECT CAST(a AS ANY) FROM t;
    ]], {
        {123},
        {4, 5, 6},
    })

test:do_catchsql_test(
    "array-6.2",
    [[
        SELECT CAST(a AS UNSIGNED) FROM t;
    ]], {
        1, "Type mismatch: can not convert array([123]) to unsigned"
    })

test:do_catchsql_test(
    "array-6.3",
    [[
        SELECT CAST(a AS STRING) FROM t;
    ]], {
        1, "Type mismatch: can not convert array([123]) to string"
    })

test:do_catchsql_test(
    "array-6.4",
    [[
        SELECT CAST(a AS NUMBER) FROM t;
    ]], {
        1, "Type mismatch: can not convert array([123]) to number"
    })

test:do_catchsql_test(
    "array-6.5",
    [[
        SELECT CAST(a AS DOUBLE) FROM t;
    ]], {
        1, "Type mismatch: can not convert array([123]) to double"
    })

test:do_catchsql_test(
    "array-6.6",
    [[
        SELECT CAST(a AS INTEGER) FROM t;
    ]], {
        1, "Type mismatch: can not convert array([123]) to integer"
    })

test:do_catchsql_test(
    "array-6.7",
    [[
        SELECT CAST(a AS BOOLEAN) FROM t;
    ]], {
        1, "Type mismatch: can not convert array([123]) to boolean"
    })

test:do_catchsql_test(
    "array-6.8",
    [[
        SELECT CAST(a AS VARBINARY) FROM t;
    ]], {
        1, "Type mismatch: can not convert array([123]) to varbinary"
    })

test:do_catchsql_test(
    "array-6.9",
    [[
        SELECT CAST(a AS SCALAR) FROM t;
    ]], {
        1, "Type mismatch: can not convert array([123]) to scalar"
    })

test:do_catchsql_test(
    "array-6.10",
    [[
        SELECT CAST(a AS DECIMAL) FROM t;
    ]], {
        1, "Type mismatch: can not convert array([123]) to decimal"
    })

test:do_catchsql_test(
    "array-6.11",
    [[
        SELECT CAST(a AS UUID) FROM t;
    ]], {
        1, "Type mismatch: can not convert array([123]) to uuid"
    })

box.execute([[CREATE TABLE t1 (id INTEGER PRIMARY KEY AUTOINCREMENT, a ANY,
                               g UNSIGNED, t STRING, n NUMBER, f DOUBLE,
                               i INTEGER, b BOOLEAN, v VARBINARY, s SCALAR,
                               d DECIMAL, u UUID);]])
box.execute([[INSERT INTO t1 VALUES(1, a1(1), 1, '1', 1, 1, 1, true, x'31', ]]..
            [[1, 1, CAST('11111111-1111-1111-1111-111111111111' AS UUID))]])

--
-- Make sure that only ANY value can be explicitly cast to ARRAY if the value
-- contains ARRAY.
--
test:do_execsql_test(
    "array-7.1",
    [[
        SELECT CAST(a AS ARRAY) FROM t1;
    ]], {
        {1}
    })

test:do_catchsql_test(
    "array-7.2",
    [[
        SELECT CAST(g AS ARRAY) FROM t1;
    ]], {
        1, "Type mismatch: can not convert integer(1) to array"
    })

test:do_catchsql_test(
    "array-7.3",
    [[
        SELECT CAST(t AS ARRAY) FROM t1;
    ]], {
        1, "Type mismatch: can not convert string('1') to array"
    })

test:do_catchsql_test(
    "array-7.4",
    [[
        SELECT CAST(n AS ARRAY) FROM t1;
    ]], {
        1, "Type mismatch: can not convert number(1) to array"
    })

test:do_catchsql_test(
    "array-7.5",
    [[
        SELECT CAST(f AS ARRAY) FROM t1;
    ]], {
        1, "Type mismatch: can not convert double(1.0) to array"
    })

test:do_catchsql_test(
    "array-7.6",
    [[
        SELECT CAST(i AS ARRAY) FROM t1;
    ]], {
        1, "Type mismatch: can not convert integer(1) to array"
    })

test:do_catchsql_test(
    "array-7.7",
    [[
        SELECT CAST(b AS ARRAY) FROM t1;
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to array"
    })

test:do_catchsql_test(
    "array-7.8",
    [[
        SELECT CAST(v AS ARRAY) FROM t1;
    ]], {
        1, "Type mismatch: can not convert varbinary(x'31') to array"
    })

test:do_catchsql_test(
    "array-7.9",
    [[
        SELECT CAST(s AS ARRAY) FROM t1;
    ]], {
        1, "Type mismatch: can not convert scalar(1) to array"
    })

test:do_catchsql_test(
    "array-7.10",
    [[
        SELECT CAST(d AS ARRAY) FROM t1;
    ]], {
        1, "Type mismatch: can not convert decimal(1) to array"
    })

test:do_catchsql_test(
    "array-7.11",
    [[
        SELECT CAST(u AS ARRAY) FROM t1;
    ]], {
        1, "Type mismatch: can not convert "..
           "uuid(11111111-1111-1111-1111-111111111111) to array"
    })

test:do_catchsql_test(
    "array-7.12",
    [[
        SELECT CAST(CAST(1 AS ANY) AS ARRAY);
    ]], {
        1, "Type mismatch: can not convert any(1) to array"
    })

-- Make sure that ARRAY can only be implicitly cast to ANY.
test:do_execsql_test(
    "array-8.1",
    [[
        INSERT INTO t1(a) VALUES(a2(1, 2));
        SELECT a FROM t1 WHERE a IS NOT NULL;
    ]], {
        {1},
        {1, 2}
    })

test:do_catchsql_test(
    "array-8.2",
    [[
        INSERT INTO t1(g) VALUES(a2(1, 2));
    ]], {
        1, "Type mismatch: can not convert array([1, 2]) to unsigned"
    })

test:do_catchsql_test(
    "array-8.3",
    [[
        INSERT INTO t1(t) VALUES(a2(1, 2));
    ]], {
        1, "Type mismatch: can not convert array([1, 2]) to string"
    })

test:do_catchsql_test(
    "array-8.4",
    [[
        INSERT INTO t1(n) VALUES(a2(1, 2));
    ]], {
        1, "Type mismatch: can not convert array([1, 2]) to number"
    })

test:do_catchsql_test(
    "array-8.5",
    [[
        INSERT INTO t1(f) VALUES(a2(1, 2));
    ]], {
        1, "Type mismatch: can not convert array([1, 2]) to double"
    })

test:do_catchsql_test(
    "array-8.6",
    [[
        INSERT INTO t1(i) VALUES(a2(1, 2));
    ]], {
        1, "Type mismatch: can not convert array([1, 2]) to integer"
    })

test:do_catchsql_test(
    "array-8.7",
    [[
        INSERT INTO t1(b) VALUES(a2(1, 2));
    ]], {
        1, "Type mismatch: can not convert array([1, 2]) to boolean"
    })

test:do_catchsql_test(
    "array-8.8",
    [[
        INSERT INTO t1(v) VALUES(a2(1, 2));
    ]], {
        1, "Type mismatch: can not convert array([1, 2]) to varbinary"
    })

test:do_catchsql_test(
    "array-8.9",
    [[
        INSERT INTO t1(s) VALUES(a2(1, 2));
    ]], {
        1, "Type mismatch: can not convert array([1, 2]) to scalar"
    })

test:do_catchsql_test(
    "array-8.10",
    [[
        INSERT INTO t1(d) VALUES(a2(1, 2));
    ]], {
        1, "Type mismatch: can not convert array([1, 2]) to decimal"
    })

test:do_catchsql_test(
    "array-8.11",
    [[
        INSERT INTO t1(u) VALUES(a2(1, 2));
    ]], {
        1, "Type mismatch: can not convert array([1, 2]) to uuid"
    })

-- Make sure nothing can be implicitly cast to ARRAY.
test:do_catchsql_test(
    "array-9.1",
    [[
        INSERT INTO t(a) VALUES(CAST(a1(1) AS ANY));
    ]], {
        1, "Type mismatch: can not convert any([1]) to array"
    })

test:do_catchsql_test(
    "array-9.2",
    [[
        INSERT INTO t(a) SELECT g FROM t1;
    ]], {
        1, "Type mismatch: can not convert integer(1) to array"
    })

test:do_catchsql_test(
    "array-9.3",
    [[
        INSERT INTO t(a) SELECT t FROM t1;
    ]], {
        1, "Type mismatch: can not convert string('1') to array"
    })

test:do_catchsql_test(
    "array-9.4",
    [[
        INSERT INTO t(a) SELECT n FROM t1;
    ]], {
        1, "Type mismatch: can not convert number(1) to array"
    })

test:do_catchsql_test(
    "array-9.5",
    [[
        INSERT INTO t(a) SELECT f FROM t1;
    ]], {
        1, "Type mismatch: can not convert double(1.0) to array"
    })

test:do_catchsql_test(
    "array-9.6",
    [[
        INSERT INTO t(a) SELECT i FROM t1;
    ]], {
        1, "Type mismatch: can not convert integer(1) to array"
    })

test:do_catchsql_test(
    "array-9.7",
    [[
        INSERT INTO t(a) SELECT b FROM t1;
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to array"
    })

test:do_catchsql_test(
    "array-9.8",
    [[
        INSERT INTO t(a) SELECT v FROM t1;
    ]], {
        1, "Type mismatch: can not convert varbinary(x'31') to array"
    })

test:do_catchsql_test(
    "array-9.9",
    [[
        INSERT INTO t(a) SELECT s FROM t1;
    ]], {
        1, "Type mismatch: can not convert scalar(1) to array"
    })

test:do_catchsql_test(
    "array-9.10",
    [[
        INSERT INTO t(a) SELECT d FROM t1;
    ]], {
        1, "Type mismatch: can not convert decimal(1) to array"
    })

test:do_catchsql_test(
    "array-9.11",
    [[
        INSERT INTO t(a) SELECT u FROM t1;
    ]], {
        1, "Type mismatch: can not convert "..
           "uuid(11111111-1111-1111-1111-111111111111) to array"
    })

--
-- Make sure ARRAY cannot participate in arithmetic and bitwise operations and
-- concatenation.
--
test:do_catchsql_test(
    "array-10.1",
    [[
        SELECT a3(1, 2, 3) + 1;
    ]], {
        1, "Type mismatch: can not convert array([1, 2, 3]) to integer, "..
           "decimal, double, datetime or interval"
    })

test:do_catchsql_test(
    "array-10.2",
    [[
        SELECT a3(1, 2, 3) - 1;
    ]], {
        1, "Type mismatch: can not convert array([1, 2, 3]) to integer, "..
           "decimal, double, datetime or interval"
    })

test:do_catchsql_test(
    "array-10.3",
    [[
        SELECT a3(1, 2, 3) * 1;
    ]], {
        1, "Type mismatch: can not convert array([1, 2, 3]) to integer, "..
           "decimal or double"
    })

test:do_catchsql_test(
    "array-10.4",
    [[
        SELECT a3(1, 2, 3) / 1;
    ]], {
        1, "Type mismatch: can not convert array([1, 2, 3]) to integer, "..
           "decimal or double"
    })

test:do_catchsql_test(
    "array-10.5",
    [[
        SELECT a3(1, 2, 3) % 1;
    ]], {
        1, "Type mismatch: can not convert array([1, 2, 3]) to integer"
    })

test:do_catchsql_test(
    "array-10.6",
    [[
        SELECT a3(1, 2, 3) >> 1;
    ]], {
        1, "Type mismatch: can not convert array([1, 2, 3]) to unsigned"
    })

test:do_catchsql_test(
    "array-10.7",
    [[
        SELECT a3(1, 2, 3) << 1;
    ]], {
        1, "Type mismatch: can not convert array([1, 2, 3]) to unsigned"
    })

test:do_catchsql_test(
    "array-10.8",
    [[
        SELECT a3(1, 2, 3) | 1;
    ]], {
        1, "Type mismatch: can not convert array([1, 2, 3]) to unsigned"
    })

test:do_catchsql_test(
    "array-10.9",
    [[
        SELECT a3(1, 2, 3) & 1;
    ]], {
        1, "Type mismatch: can not convert array([1, 2, 3]) to unsigned"
    })

test:do_catchsql_test(
    "array-10.10",
    [[
        SELECT ~a3(1, 2, 3);
    ]], {
        1, "Type mismatch: can not convert array([1, 2, 3]) to unsigned"
    })

test:do_catchsql_test(
    "array-10.11",
    [[
        SELECT a3(1, 2, 3) || 'asd';
    ]], {
        1, "Inconsistent types: expected string or varbinary got "..
           "array([1, 2, 3])"
    })

-- Make sure ARRAY is not comparable.
test:do_catchsql_test(
    "array-11.1",
    [[
        SELECT a1(1) > a1(2);
    ]], {
        1, "Type mismatch: can not convert array([1]) to comparable type"
    })

test:do_catchsql_test(
    "array-11.2",
    [[
        SELECT a1(1) < CAST(1 AS ANY);
    ]], {
        1, "Type mismatch: can not convert array([1]) to comparable type"
    })

test:do_catchsql_test(
    "array-11.3",
    [[
        SELECT a1(1) == CAST(1 AS SCALAR);
    ]], {
        1, "Type mismatch: can not convert array([1]) to comparable type"
    })

test:do_catchsql_test(
    "array-11.4",
    [[
        SELECT a1(1) != CAST(1 AS NUMBER);
    ]], {
        1, "Type mismatch: can not convert array([1]) to comparable type"
    })

test:do_catchsql_test(
    "array-11.5",
    [[
        SELECT a1(1) >= CAST(1 AS DECIMAL);;
    ]], {
        1, "Type mismatch: can not convert array([1]) to comparable type"
    })

test:do_catchsql_test(
    "array-11.6",
    [[
        SELECT a1(1) <= CAST(1 AS UNSIGNED);;
    ]], {
        1, "Type mismatch: can not convert array([1]) to comparable type"
    })

test:do_catchsql_test(
    "array-11.7",
    [[
        SELECT a1(1) > 1;
    ]], {
        1, "Type mismatch: can not convert array([1]) to comparable type"
    })

test:do_catchsql_test(
    "array-11.8",
    [[
        SELECT a1(1) < 1e0;
    ]], {
        1, "Type mismatch: can not convert array([1]) to comparable type"
    })

test:do_catchsql_test(
    "array-11.9",
    [[
        SELECT a1(1) == 'asd';
    ]], {
        1, "Type mismatch: can not convert array([1]) to comparable type"
    })

test:do_catchsql_test(
    "array-11.10",
    [[
        SELECT a1(1) != x'323334';
    ]], {
        1, "Type mismatch: can not convert array([1]) to comparable type"
    })

test:do_catchsql_test(
    "array-11.11",
    [[
        SELECT a1(1) >= true;
    ]], {
        1, "Type mismatch: can not convert array([1]) to comparable type"
    })

test:do_catchsql_test(
    "array-11.12",
    [[
        SELECT a1(1) <= CAST('11111111-1111-1111-1111-111111111111' AS UUID);
    ]], {
        1, "Type mismatch: can not convert array([1]) to comparable type"
    })

test:do_catchsql_test(
    "array-12.1",
    [[
        SELECT ABS(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function ABS()"
    })

test:do_catchsql_test(
    "array-12.2",
    [[
        SELECT AVG(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function AVG()"
    })

test:do_catchsql_test(
    "array-12.3",
    [[
        SELECT CHAR(a) FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function CHAR()"
    })

test:do_catchsql_test(
    "array-12.4",
    [[
        SELECT CHARACTER_LENGTH(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "CHARACTER_LENGTH()"
    })

test:do_catchsql_test(
    "array-12.5",
    [[
        SELECT CHAR_LENGTH(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "CHAR_LENGTH()"
    })

test:do_execsql_test(
    "array-12.6",
    [[
        SELECT COALESCE(NULL, a) FROM t;
    ]], {
        {123},
        {4, 5, 6}
    })

test:do_execsql_test(
    "array-12.7",
    [[
        SELECT COUNT(a) FROM t;
    ]], {
        2
    })

test:do_catchsql_test(
    "array-12.8",
    [[
        SELECT GREATEST(1, a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "GREATEST()"
    })

test:do_catchsql_test(
    "array-12.9",
    [[
        SELECT GROUP_CONCAT(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "GROUP_CONCAT()"
    })

test:do_catchsql_test(
    "array-12.10",
    [[
        SELECT HEX(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function HEX()"
    })

test:do_execsql_test(
    "array-12.11",
    [[
        SELECT IFNULL(a, 1) FROM t;
    ]], {
        {123},
        {4, 5, 6}
    })

test:do_catchsql_test(
    "array-12.12",
    [[
        SELECT LEAST(1, a) FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function LEAST()"
    })

test:do_catchsql_test(
    "array-12.13",
    [[
        SELECT LENGTH(a) FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function LENGTH()"
    })

test:do_catchsql_test(
    "array-12.14",
    [[
        SELECT 'asd' LIKE a FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function LIKE()"
    })

test:do_execsql_test(
    "array-12.15",
    [[
        SELECT LIKELIHOOD(a, 0.5e0) FROM t;
    ]], {
        {123},
        {4, 5, 6}
    })

test:do_execsql_test(
    "array-12.16",
    [[
        SELECT LIKELY(a) FROM t;
    ]], {
        {123},
        {4, 5, 6}
    })

test:do_catchsql_test(
    "array-12.17",
    [[
        SELECT LOWER(a) FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function LOWER()"
    })

test:do_catchsql_test(
    "array-12.18",
    [[
        SELECT MAX(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function MAX()"
    })

test:do_catchsql_test(
    "array-12.19",
    [[
        SELECT MIN(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function MIN()"
    })

test:do_catchsql_test(
    "array-12.20",
    [[
        SELECT NULLIF(1, a) FROM t;
    ]], {
        1, "Type mismatch: can not convert array([123]) to scalar"
    })

test:do_catchsql_test(
    "array-12.21",
    [[
        SELECT POSITION('asd', a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "POSITION()"
    })

test:do_execsql_test(
    "array-12.22",
    [[
        SELECT PRINTF(a) FROM t;
    ]], {
        "[123]",
        "[4, 5, 6]"
    })

test:do_execsql_test(
    "array-12.23",
    [[
        SELECT QUOTE(a) FROM t;
    ]], {
        "[123]",
        "[4, 5, 6]"
    })

test:do_catchsql_test(
    "array-12.24",
    [[
        SELECT RANDOMBLOB(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "RANDOMBLOB()"
    })

test:do_catchsql_test(
    "array-12.25",
    [[
        SELECT REPLACE('asd', 'a', a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "REPLACE()"
    })

test:do_catchsql_test(
    "array-12.26",
    [[
        SELECT ROUND(a) FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function ROUND()"
    })

test:do_catchsql_test(
    "array-12.27",
    [[
        SELECT SOUNDEX(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "SOUNDEX()"
    })

test:do_catchsql_test(
    "array-12.28",
    [[
        SELECT SUBSTR(a, 1, 1) FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function SUBSTR()"
    })

test:do_catchsql_test(
    "array-12.29",
    [[
        SELECT SUM(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function SUM()"
    })

test:do_catchsql_test(
    "array-12.30",
    [[
        SELECT TOTAL(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "TOTAL()"
    })

test:do_catchsql_test(
    "array-12.31",
    [[
        SELECT TRIM(a) FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function TRIM()"
    })

test:do_execsql_test(
    "array-12.32",
    [[
        SELECT TYPEOF(a) FROM t;
    ]], {
        "array", "array"
    })

test:do_catchsql_test(
    "array-12.33",
    [[
        SELECT UNICODE(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "UNICODE()"
    })

test:do_execsql_test(
    "array-12.34",
    [[
        SELECT UNLIKELY(a) FROM t;
    ]], {
        {123},
        {4, 5, 6}
    })

test:do_catchsql_test(
    "array-12.35",
    [[
        SELECT UPPER(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function UPPER()"
    })

test:do_catchsql_test(
    "array-12.36",
    [[
        SELECT UUID(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function UUID()"
    })

test:do_catchsql_test(
    "array-12.37",
    [[
        SELECT ZEROBLOB(a) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function ZEROBLOB()"
    })

-- Make sure syntax for ARRAY values works as intended.
test:do_execsql_test(
    "array-13.1",
    [[
        SELECT [a, g, t, n, f, i, b, v, s, d, u] FROM t1 WHERE id = 1;
    ]], {
        {{1}, 1, '1', 1, 1, 1, true, require('varbinary').new('1'), 1,
         require('decimal').new(1),
         require('uuid').fromstr('11111111-1111-1111-1111-111111111111')}
    })

test:do_execsql_test(
    "array-13.2",
    [[
        SELECT [1, true, 1.5e0, ['asd', x'32'], 1234.0];
    ]], {
        {1, true, 1.5, {'asd', require('varbinary').new('2')},
         require('decimal').new(1234)}
    })

test:do_execsql_test(
    "array-13.3",
    [[
        SELECT [];
    ]], {
        {}
    })

local arr = {0}
local arr_str = '0'
for i = 1, 1000 do table.insert(arr, i) arr_str = arr_str .. ', ' .. i end
test:do_execsql_test(
    "array-13.4",
    [[
        SELECT []] .. arr_str .. [[];
    ]], {
        arr
    })

test:do_execsql_test(
    "array-13.5",
    [[
        SELECT typeof([1]);
    ]], {
        "array"
    })

-- Make sure that ARRAY values can be used as bound variable.
test:do_test(
    "builtins-14.1",
    function()
        local res = box.execute([[SELECT #a;]], {{['#a'] = {1, 2, 3}}})
        return {res.rows[1][1]}
    end, {
        {1, 2, 3}
    })

local remote = require('net.box')
box.cfg{listen = os.getenv('LISTEN')}
box.schema.user.grant('guest', 'execute', 'sql')
local cn = remote.connect(box.cfg.listen)
test:do_test(
    "builtins-14.2",
    function()
        local res = cn:execute([[SELECT #a;]], {{['#a'] = {1, 2, 3}}})
        return {res.rows[1][1]}
    end, {
        {1, 2, 3}
    })
cn:close()
box.schema.user.revoke('guest', 'execute', 'sql')

box.execute([[DROP TABLE t1;]])
box.execute([[DROP TABLE t;]])

test:finish_test()
