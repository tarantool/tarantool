#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(112)

box.schema.func.create('m1', {
    language = 'Lua',
    body = 'function(a, b) local m = {[a] = b} '..
           'return setmetatable(m, { __serialize = "map" }) end',
    returns = 'map',
    param_list = {'any', 'any'},
    exports = {'LUA', 'SQL'}
});

box.schema.func.create('m2', {
    language = 'Lua',
    body = 'function(a, b, c, d) local m = {[a] = b, [c] = d} '..
           'return setmetatable(m, { __serialize = "map" }) end',
    returns = 'map',
    param_list = {'any', 'any', 'any', 'any'},
    exports = {'LUA', 'SQL'}
});

box.schema.func.create('m3', {
    language = 'Lua',
    body = 'function(a, b, c, d, e, f) local m = {[a] = b, [c] = d, [e] = f} '..
           'return setmetatable(m, { __serialize = "map" }) end',
    returns = 'map',
    param_list = {'any', 'any', 'any', 'any', 'any', 'any'},
    exports = {'LUA', 'SQL'}
});

-- Make sure it is possible to create tables with field type MAP.
test:do_execsql_test(
    "map-1",
    [[
        CREATE TABLE t (i INT PRIMARY KEY AUTOINCREMENT, m MAP);
    ]], {
    })

box.space.t:insert({0, {a = 1, b = 2}})

-- Make sure it is possible to select from MAP field.
test:do_execsql_test(
    "map-2",
    [[
        SELECT i, m FROM t;
    ]], {
        0, {a = 1, b = 2},
    })

-- Make sure it is possible to insert into MAP field.
test:do_execsql_test(
    "map-3",
    [[
        INSERT INTO t(m) VALUES(NULL);
        INSERT INTO t(m) VALUES(m1('a', 1));
        INSERT INTO t(m) VALUES(m2('b', 2, 'c', 3));
        INSERT INTO t(m) VALUES(m3('d', 4, 'e', 5, 'f', 6));
        SELECT i, m FROM t;
    ]], {
        0, {a = 1, b = 2},
        1, "",
        2, {a = 1},
        3, {b = 2, c = 3},
        4, {d = 4, e = 5, f = 6},
    })

-- Make sure it is possible to delete from MAP field.
test:do_execsql_test(
    "map-4",
    [[
        DELETE FROM t WHERE i < 3;
        SELECT i, m FROM t;
    ]], {
        3, {b = 2, c = 3},
        4, {d = 4, e = 5, f = 6},
    })

-- Make sure it is possible to update MAP field.
test:do_execsql_test(
    "map-5",
    [[
        UPDATE t SET m = m1('abc', 123) WHERE i = 3;
        SELECT i, m FROM t;
    ]], {
        3, {abc = 123},
        4, {d = 4, e = 5, f = 6},
    })

-- Make sure MAP can only be explicitly cast to ANY.
test:do_execsql_test(
    "map-6.1",
    [[
        SELECT CAST(m AS ANY) FROM t;
    ]], {
        {abc = 123},
        {d = 4, e = 5, f = 6},
    })

test:do_catchsql_test(
    "map-6.2",
    [[
        SELECT CAST(m AS UNSIGNED) FROM t;
    ]], {
        1, 'Type mismatch: can not convert map({"abc": 123}) to unsigned'
    })

test:do_catchsql_test(
    "map-6.3",
    [[
        SELECT CAST(m AS STRING) FROM t;
    ]], {
        1, 'Type mismatch: can not convert map({"abc": 123}) to string'
    })

test:do_catchsql_test(
    "map-6.4",
    [[
        SELECT CAST(m AS NUMBER) FROM t;
    ]], {
        1, 'Type mismatch: can not convert map({"abc": 123}) to number'
    })

test:do_catchsql_test(
    "map-6.5",
    [[
        SELECT CAST(m AS DOUBLE) FROM t;
    ]], {
        1, 'Type mismatch: can not convert map({"abc": 123}) to double'
    })

test:do_catchsql_test(
    "map-6.6",
    [[
        SELECT CAST(m AS INTEGER) FROM t;
    ]], {
        1, 'Type mismatch: can not convert map({"abc": 123}) to integer'
    })

test:do_catchsql_test(
    "map-6.7",
    [[
        SELECT CAST(m AS BOOLEAN) FROM t;
    ]], {
        1, 'Type mismatch: can not convert map({"abc": 123}) to boolean'
    })

test:do_catchsql_test(
    "map-6.8",
    [[
        SELECT CAST(m AS VARBINARY) FROM t;
    ]], {
        1, 'Type mismatch: can not convert map({"abc": 123}) to varbinary'
    })

test:do_catchsql_test(
    "map-6.9",
    [[
        SELECT CAST(m AS SCALAR) FROM t;
    ]], {
        1, 'Type mismatch: can not convert map({"abc": 123}) to scalar'
    })

test:do_catchsql_test(
    "map-6.10",
    [[
        SELECT CAST(m AS DECIMAL) FROM t;
    ]], {
        1, 'Type mismatch: can not convert map({"abc": 123}) to decimal'
    })

test:do_catchsql_test(
    "map-6.11",
    [[
        SELECT CAST(m AS UUID) FROM t;
    ]], {
        1, 'Type mismatch: can not convert map({"abc": 123}) to uuid'
    })

box.execute([[CREATE TABLE t1 (id INTEGER PRIMARY KEY AUTOINCREMENT, a ANY,
                               g UNSIGNED, t STRING, n NUMBER, f DOUBLE,
                               i INTEGER, b BOOLEAN, v VARBINARY, s SCALAR,
                               d DECIMAL, u UUID);]])
box.execute([[INSERT INTO t1 VALUES(1, m1('a', 1), 1, '1', 1, 1, 1, true, ]]..
            [[x'31', 1, 1, ]]..
            [[CAST('11111111-1111-1111-1111-111111111111' AS UUID))]])

--
-- Make sure that only ANY value can be explicitly cast to MAP if the value
-- contains MAP.
--
test:do_execsql_test(
    "map-7.1",
    [[
        SELECT CAST(a AS MAP) FROM t1;
    ]], {
        {a = 1}
    })

test:do_catchsql_test(
    "map-7.2",
    [[
        SELECT CAST(g AS MAP) FROM t1;
    ]], {
        1, "Type mismatch: can not convert integer(1) to map"
    })

test:do_catchsql_test(
    "map-7.3",
    [[
        SELECT CAST(t AS MAP) FROM t1;
    ]], {
        1, "Type mismatch: can not convert string('1') to map"
    })

test:do_catchsql_test(
    "map-7.4",
    [[
        SELECT CAST(n AS MAP) FROM t1;
    ]], {
        1, "Type mismatch: can not convert number(1) to map"
    })

test:do_catchsql_test(
    "map-7.5",
    [[
        SELECT CAST(f AS MAP) FROM t1;
    ]], {
        1, "Type mismatch: can not convert double(1.0) to map"
    })

test:do_catchsql_test(
    "map-7.6",
    [[
        SELECT CAST(i AS MAP) FROM t1;
    ]], {
        1, "Type mismatch: can not convert integer(1) to map"
    })

test:do_catchsql_test(
    "map-7.7",
    [[
        SELECT CAST(b AS MAP) FROM t1;
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to map"
    })

test:do_catchsql_test(
    "map-7.8",
    [[
        SELECT CAST(v AS MAP) FROM t1;
    ]], {
        1, "Type mismatch: can not convert varbinary(x'31') to map"
    })

test:do_catchsql_test(
    "map-7.9",
    [[
        SELECT CAST(s AS MAP) FROM t1;
    ]], {
        1, "Type mismatch: can not convert scalar(1) to map"
    })

test:do_catchsql_test(
    "map-7.10",
    [[
        SELECT CAST(d AS MAP) FROM t1;
    ]], {
        1, "Type mismatch: can not convert decimal(1) to map"
    })

test:do_catchsql_test(
    "map-7.11",
    [[
        SELECT CAST(u AS MAP) FROM t1;
    ]], {
        1, "Type mismatch: can not convert "..
           "uuid(11111111-1111-1111-1111-111111111111) to map"
    })

test:do_catchsql_test(
    "map-7.12",
    [[
        SELECT CAST(CAST(1 AS ANY) AS MAP);
    ]], {
        1, "Type mismatch: can not convert any(1) to map"
    })

-- Make sure that MAP can only be implicitly cast to ANY.
test:do_execsql_test(
    "map-8.1",
    [[
        INSERT INTO t1(a) VALUES(m1(1, 2));
        SELECT a FROM t1 WHERE a IS NOT NULL;
    ]], {
        {a = 1},
        {[1] = 2}
    })

test:do_catchsql_test(
    "map-8.2",
    [[
        INSERT INTO t1(g) VALUES(m1(1, 2));
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to unsigned'
    })

test:do_catchsql_test(
    "map-8.3",
    [[
        INSERT INTO t1(t) VALUES(m1(1, 2));
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to string'
    })

test:do_catchsql_test(
    "map-8.4",
    [[
        INSERT INTO t1(n) VALUES(m1(1, 2));
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to number'
    })

test:do_catchsql_test(
    "map-8.5",
    [[
        INSERT INTO t1(f) VALUES(m1(1, 2));
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to double'
    })

test:do_catchsql_test(
    "map-8.6",
    [[
        INSERT INTO t1(i) VALUES(m1(1, 2));
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to integer'
    })

test:do_catchsql_test(
    "map-8.7",
    [[
        INSERT INTO t1(b) VALUES(m1(1, 2));
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to boolean'
    })

test:do_catchsql_test(
    "map-8.8",
    [[
        INSERT INTO t1(v) VALUES(m1(1, 2));
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to varbinary'
    })

test:do_catchsql_test(
    "map-8.9",
    [[
        INSERT INTO t1(s) VALUES(m1(1, 2));
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to scalar'
    })

test:do_catchsql_test(
    "map-8.10",
    [[
        INSERT INTO t1(d) VALUES(m1(1, 2));
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to decimal'
    })

test:do_catchsql_test(
    "map-8.11",
    [[
        INSERT INTO t1(u) VALUES(m1(1, 2));
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to uuid'
    })

-- Make sure nothing can be implicitly cast to MAP.
test:do_catchsql_test(
    "map-9.1",
    [[
        INSERT INTO t(m) VALUES(CAST(m1('a', 1) AS ANY));
    ]], {
        1, 'Type mismatch: can not convert any({"a": 1}) to map'
    })

test:do_catchsql_test(
    "map-9.2",
    [[
        INSERT INTO t(m) SELECT g FROM t1;
    ]], {
        1, "Type mismatch: can not convert integer(1) to map"
    })

test:do_catchsql_test(
    "map-9.3",
    [[
        INSERT INTO t(m) SELECT t FROM t1;
    ]], {
        1, "Type mismatch: can not convert string('1') to map"
    })

test:do_catchsql_test(
    "map-9.4",
    [[
        INSERT INTO t(m) SELECT n FROM t1;
    ]], {
        1, "Type mismatch: can not convert number(1) to map"
    })

test:do_catchsql_test(
    "map-9.5",
    [[
        INSERT INTO t(m) SELECT f FROM t1;
    ]], {
        1, "Type mismatch: can not convert double(1.0) to map"
    })

test:do_catchsql_test(
    "map-9.6",
    [[
        INSERT INTO t(m) SELECT i FROM t1;
    ]], {
        1, "Type mismatch: can not convert integer(1) to map"
    })

test:do_catchsql_test(
    "map-9.7",
    [[
        INSERT INTO t(m) SELECT b FROM t1;
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to map"
    })

test:do_catchsql_test(
    "map-9.8",
    [[
        INSERT INTO t(m) SELECT v FROM t1;
    ]], {
        1, "Type mismatch: can not convert varbinary(x'31') to map"
    })

test:do_catchsql_test(
    "map-9.9",
    [[
        INSERT INTO t(m) SELECT s FROM t1;
    ]], {
        1, "Type mismatch: can not convert scalar(1) to map"
    })

test:do_catchsql_test(
    "map-9.10",
    [[
        INSERT INTO t(m) SELECT d FROM t1;
    ]], {
        1, "Type mismatch: can not convert decimal(1) to map"
    })

test:do_catchsql_test(
    "map-9.11",
    [[
        INSERT INTO t(m) SELECT u FROM t1;
    ]], {
        1, "Type mismatch: can not convert "..
           "uuid(11111111-1111-1111-1111-111111111111) to map"
    })

--
-- Make sure MAP cannot participate in arithmetic and bitwise operations and
-- concatenation.
--
test:do_catchsql_test(
    "map-10.1",
    [[
        SELECT m1(1, 2) + 1;
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to integer, '..
           "decimal, double, datetime or interval"
    })

test:do_catchsql_test(
    "map-10.2",
    [[
        SELECT m1(1, 2) - 1;
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to integer, '..
           "decimal, double, datetime or interval"
    })

test:do_catchsql_test(
    "map-10.3",
    [[
        SELECT m1(1, 2) * 1;
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to integer, '..
           "decimal or double"
    })

test:do_catchsql_test(
    "map-10.4",
    [[
        SELECT m1(1, 2) / 1;
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to integer, '..
           "decimal or double"
    })

test:do_catchsql_test(
    "map-10.5",
    [[
        SELECT m1(1, 2) % 1;
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to integer'
    })

test:do_catchsql_test(
    "map-10.6",
    [[
        SELECT m1(1, 2) >> 1;
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to unsigned'
    })

test:do_catchsql_test(
    "map-10.7",
    [[
        SELECT m1(1, 2) << 1;
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to unsigned'
    })

test:do_catchsql_test(
    "map-10.8",
    [[
        SELECT m1(1, 2) | 1;
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to unsigned'
    })

test:do_catchsql_test(
    "map-10.9",
    [[
        SELECT m1(1, 2) & 1;
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to unsigned'
    })

test:do_catchsql_test(
    "map-10.10",
    [[
        SELECT ~m1(1, 2);
    ]], {
        1, 'Type mismatch: can not convert map({1: 2}) to unsigned'
    })

test:do_catchsql_test(
    "map-10.11",
    [[
        SELECT m1(1, 2) || 'asd';
    ]], {
        1, 'Inconsistent types: expected string or varbinary got map({1: 2})'
    })

-- Make sure MAP is not comparable.
test:do_catchsql_test(
    "map-11.1",
    [[
        SELECT m1('a', 1) > m1('b', 2);
    ]], {
        1, 'Type mismatch: can not convert map({"a": 1}) to comparable type'
    })

test:do_catchsql_test(
    "map-11.2",
    [[
        SELECT m1('a', 1) < CAST(1 AS ANY);
    ]], {
        1, 'Type mismatch: can not convert map({"a": 1}) to comparable type'
    })

test:do_catchsql_test(
    "map-11.3",
    [[
        SELECT m1('a', 1) == CAST(1 AS SCALAR);
    ]], {
        1, 'Type mismatch: can not convert map({"a": 1}) to comparable type'
    })

test:do_catchsql_test(
    "map-11.4",
    [[
        SELECT m1('a', 1) != CAST(1 AS NUMBER);
    ]], {
        1, 'Type mismatch: can not convert map({"a": 1}) to comparable type'
    })

test:do_catchsql_test(
    "map-11.5",
    [[
        SELECT m1('a', 1) >= CAST(1 AS DECIMAL);;
    ]], {
        1, 'Type mismatch: can not convert map({"a": 1}) to comparable type'
    })

test:do_catchsql_test(
    "map-11.6",
    [[
        SELECT m1('a', 1) <= CAST(1 AS UNSIGNED);;
    ]], {
        1, 'Type mismatch: can not convert map({"a": 1}) to comparable type'
    })

test:do_catchsql_test(
    "map-11.7",
    [[
        SELECT m1('a', 1) > 1;
    ]], {
        1, 'Type mismatch: can not convert map({"a": 1}) to comparable type'
    })

test:do_catchsql_test(
    "map-11.8",
    [[
        SELECT m1('a', 1) < 1e0;
    ]], {
        1, 'Type mismatch: can not convert map({"a": 1}) to comparable type'
    })

test:do_catchsql_test(
    "map-11.9",
    [[
        SELECT m1('a', 1) == 'asd';
    ]], {
        1, 'Type mismatch: can not convert map({"a": 1}) to comparable type'
    })

test:do_catchsql_test(
    "map-11.10",
    [[
        SELECT m1('a', 1) != x'323334';
    ]], {
        1, 'Type mismatch: can not convert map({"a": 1}) to comparable type'
    })

test:do_catchsql_test(
    "map-11.11",
    [[
        SELECT m1('a', 1) >= true;
    ]], {
        1, 'Type mismatch: can not convert map({"a": 1}) to comparable type'
    })

test:do_catchsql_test(
    "map-11.12",
    [[
        SELECT m1('a', 1) <= CAST('11111111-1111-1111-1111-111111111111' AS UUID);
    ]], {
        1, 'Type mismatch: can not convert map({"a": 1}) to comparable type'
    })

test:do_catchsql_test(
    "map-12.1",
    [[
        SELECT ABS(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function ABS()"
    })

test:do_catchsql_test(
    "map-12.2",
    [[
        SELECT AVG(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function AVG()"
    })

test:do_catchsql_test(
    "map-12.3",
    [[
        SELECT CHAR(m) FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function CHAR()"
    })

test:do_catchsql_test(
    "map-12.4",
    [[
        SELECT CHARACTER_LENGTH(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "CHARACTER_LENGTH()"
    })

test:do_catchsql_test(
    "map-12.5",
    [[
        SELECT CHAR_LENGTH(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "CHAR_LENGTH()"
    })

test:do_execsql_test(
    "map-12.6",
    [[
        SELECT COALESCE(NULL, m) FROM t;
    ]], {
        {abc = 123},
        {d = 4, e = 5, f = 6},
    })

test:do_execsql_test(
    "map-12.7",
    [[
        SELECT COUNT(m) FROM t;
    ]], {
        2
    })

test:do_catchsql_test(
    "map-12.8",
    [[
        SELECT GREATEST(1, m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "GREATEST()"
    })

test:do_catchsql_test(
    "map-12.9",
    [[
        SELECT GROUP_CONCAT(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "GROUP_CONCAT()"
    })

test:do_catchsql_test(
    "map-12.10",
    [[
        SELECT HEX(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function HEX()"
    })

test:do_execsql_test(
    "map-12.11",
    [[
        SELECT IFNULL(m, 1) FROM t;
    ]], {
        {abc = 123},
        {d = 4, e = 5, f = 6},
    })

test:do_catchsql_test(
    "map-12.12",
    [[
        SELECT LEAST(1, m) FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function LEAST()"
    })

test:do_catchsql_test(
    "map-12.13",
    [[
        SELECT LENGTH(m) FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function LENGTH()"
    })

test:do_catchsql_test(
    "map-12.14",
    [[
        SELECT 'asd' LIKE m FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function LIKE()"
    })

test:do_execsql_test(
    "map-12.15",
    [[
        SELECT LIKELIHOOD(m, 0.5e0) FROM t;
    ]], {
        {abc = 123},
        {d = 4, e = 5, f = 6},
    })

test:do_execsql_test(
    "map-12.16",
    [[
        SELECT LIKELY(m) FROM t;
    ]], {
        {abc = 123},
        {d = 4, e = 5, f = 6},
    })

test:do_catchsql_test(
    "map-12.17",
    [[
        SELECT LOWER(m) FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function LOWER()"
    })

test:do_catchsql_test(
    "map-12.18",
    [[
        SELECT MAX(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function MAX()"
    })

test:do_catchsql_test(
    "map-12.19",
    [[
        SELECT MIN(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function MIN()"
    })

test:do_catchsql_test(
    "map-12.20",
    [[
        SELECT NULLIF(1, m) FROM t;
    ]], {
        1, [[Type mismatch: can not convert map({"abc": 123}) to scalar]]
    })

test:do_catchsql_test(
    "map-12.21",
    [[
        SELECT POSITION('asd', m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "POSITION()"
    })

test:do_execsql_test(
    "map-12.22",
    [[
        SELECT PRINTF(m) FROM t;
    ]], {
        '{"abc": 123}',
        '{"d": 4, "f": 6, "e": 5}',
    })

test:do_execsql_test(
    "map-12.23",
    [[
        SELECT QUOTE(m) FROM t;
    ]], {
        '{"abc": 123}',
        '{"d": 4, "f": 6, "e": 5}',
    })

test:do_catchsql_test(
    "map-12.24",
    [[
        SELECT RANDOMBLOB(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "RANDOMBLOB()"
    })

test:do_catchsql_test(
    "map-12.25",
    [[
        SELECT REPLACE('asd', 'a', m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "REPLACE()"
    })

test:do_catchsql_test(
    "map-12.26",
    [[
        SELECT ROUND(m) FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function ROUND()"
    })

test:do_catchsql_test(
    "map-12.27",
    [[
        SELECT SOUNDEX(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "SOUNDEX()"
    })

test:do_catchsql_test(
    "map-12.28",
    [[
        SELECT SUBSTR(m, 1, 1) FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function SUBSTR()"
    })

test:do_catchsql_test(
    "map-12.29",
    [[
        SELECT SUM(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function SUM()"
    })

test:do_catchsql_test(
    "map-12.30",
    [[
        SELECT TOTAL(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "TOTAL()"
    })

test:do_catchsql_test(
    "map-12.31",
    [[
        SELECT TRIM(m) FROM t;
    ]], {
        1,
        "Failed to execute SQL statement: wrong arguments for function TRIM()"
    })

test:do_execsql_test(
    "map-12.32",
    [[
        SELECT TYPEOF(m) FROM t;
    ]], {
        "map", "map"
    })

test:do_catchsql_test(
    "map-12.33",
    [[
        SELECT UNICODE(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function "..
           "UNICODE()"
    })

test:do_execsql_test(
    "map-12.34",
    [[
        SELECT UNLIKELY(m) FROM t;
    ]], {
        {abc = 123},
        {d = 4, e = 5, f = 6},
    })

test:do_catchsql_test(
    "map-12.35",
    [[
        SELECT UPPER(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function UPPER()"
    })

test:do_catchsql_test(
    "map-12.36",
    [[
        SELECT UUID(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function UUID()"
    })

test:do_catchsql_test(
    "map-12.37",
    [[
        SELECT ZEROBLOB(m) FROM t;
    ]], {
        1, "Failed to execute SQL statement: wrong arguments for function ZEROBLOB()"
    })

-- Make sure that MAP values can be used as a bound variable.
test:do_test(
    "builtins-13.1",
    function()
        local res = box.execute([[SELECT #a;]], {{['#a'] = {abc = 2, [1] = 3}}})
        return {res.rows[1][1]}
    end, {
        {abc = 2, [1] = 3}
    })

local remote = require('net.box')
box.cfg{listen = os.getenv('LISTEN')}
box.schema.user.grant('guest', 'execute', 'sql')
local cn = remote.connect(box.cfg.listen)
test:do_test(
    "builtins-13.2",
    function()
        local res = cn:execute([[SELECT #a;]], {{['#a'] = {abc = 2, [1] = 3}}})
        return {res.rows[1][1]}
    end, {
        {abc = 2, [1] = 3}
    })
cn:close()
box.schema.user.revoke('guest', 'execute', 'sql')

box.execute([[DROP TABLE t1;]])
box.execute([[DROP TABLE t;]])

test:finish_test()
