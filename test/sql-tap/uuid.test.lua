#!/usr/bin/env tarantool
local build_path = os.getenv("BUILDDIR")
package.cpath = build_path..'/test/sql-tap/?.so;'..build_path..'/test/sql-tap/?.dylib;'..package.cpath

local test = require("sqltester")
test:plan(147)

local uuid = require("uuid")
local uuid1 = uuid.fromstr("11111111-1111-1111-1111-111111111111")
local uuid2 = uuid.fromstr("22222222-1111-1111-1111-111111111111")
local uuid3 = uuid.fromstr("11111111-3333-1111-1111-111111111111")

-- Check that it is possible to create spaces with UUID field.
test:do_execsql_test(
    "uuid-1",
    [[
        CREATE TABLE t1 (i INT PRIMARY KEY, u UUID);
        CREATE TABLE t2 (u UUID PRIMARY KEY);
    ]], {
    })

box.space.T1:insert({1, uuid1})
box.space.T1:insert({2, uuid2})
box.space.T1:insert({3, uuid3})
box.space.T1:insert({4, uuid1})
box.space.T1:insert({5, uuid1})
box.space.T1:insert({6, uuid2})
box.space.T2:insert({uuid1})
box.space.T2:insert({uuid2})
box.space.T2:insert({uuid3})

-- Check that SELECT can work with UUID.
test:do_execsql_test(
    "uuid-2.1.1",
    [[
        SELECT * FROM t1;
    ]], {
        1, uuid1, 2, uuid2, 3, uuid3, 4, uuid1, 5, uuid1, 6, uuid2
    })

test:do_execsql_test(
    "uuid-2.1.2",
    [[
        SELECT * FROM t2;
    ]], {
        uuid1, uuid3, uuid2
    })

-- Check that ORDER BY can work with UUID.
test:do_execsql_test(
    "uuid-2.2.1",
    [[
        SELECT * FROM t1 ORDER BY u;
    ]], {
        1, uuid1, 4, uuid1, 5, uuid1, 3, uuid3, 2, uuid2, 6, uuid2
    })

test:do_execsql_test(
    "uuid-2.2.2",
    [[
        SELECT * FROM t1 ORDER BY u DESC;
    ]], {
        2, uuid2, 6, uuid2, 3, uuid3, 1, uuid1, 4, uuid1, 5, uuid1
    })

test:do_execsql_test(
    "uuid-2.2.3",
    [[
        SELECT * FROM t2 ORDER BY u;
    ]], {
        uuid1, uuid3, uuid2
    })

test:do_execsql_test(
    "uuid-2.2.4",
    [[
        SELECT * FROM t2 ORDER BY u DESC;
    ]], {
        uuid2, uuid3, uuid1
    })

-- Check that GROUP BY can work with UUID.
test:do_execsql_test(
    "uuid-2.3.1",
    [[
        SELECT count(*), u FROM t1 GROUP BY u;
    ]], {
        3, uuid1, 1, uuid3, 2, uuid2
    })

test:do_execsql_test(
    "uuid-2.3.2",
    [[
        SELECT count(*), u FROM t2 GROUP BY u;
    ]], {
        1, uuid1, 1, uuid3, 1, uuid2
    })

-- Check that subselects can work with UUID.
test:do_execsql_test(
    "uuid-2.4",
    [[
        SELECT * FROM (SELECT * FROM (SELECT * FROM t2 LIMIT 2) LIMIT 2 OFFSET 1);
    ]], {
        uuid3
    })

-- Check that DISTINCT can work with UUID.
test:do_execsql_test(
    "uuid-2.5",
    [[
        SELECT DISTINCT u FROM t1;
    ]], {
        uuid1, uuid2, uuid3
    })

-- Check that VIEW can work with UUID.
test:do_execsql_test(
    "uuid-2.6",
    [[
        CREATE VIEW v AS SELECT u FROM t1;
        SELECT * FROM v;
    ]], {
        uuid1, uuid2, uuid3, uuid1, uuid1, uuid2
    })

-- Check that LIMIT does not accept UUID as argument.
test:do_catchsql_test(
    "uuid-3.1",
    [[
        SELECT 1 LIMIT (SELECT u FROM t1 LIMIT 1);
    ]], {
        1, "Failed to execute SQL statement: Only positive integers are allowed in the LIMIT clause"
    })

-- Check that OFFSET does not accept UUID as argument.
test:do_catchsql_test(
    "uuid-3.2",
    [[
        SELECT 1 LIMIT 1 OFFSET (SELECT u FROM t1 LIMIT 1);
    ]], {
        1, "Failed to execute SQL statement: Only positive integers are allowed in the OFFSET clause"
    })

-- Check that ephemeral space can work with UUID.
test:do_execsql_test(
    "uuid-4",
    [[
        EXPLAIN SELECT * from (VALUES(1)), t2;
    ]], {
        "/OpenTEphemeral/"
    })

test:execsql([[
    CREATE TABLE t5f (u UUID PRIMARY KEY, f UUID REFERENCES t5f(u));
    CREATE TABLE t5c (i INT PRIMARY KEY, f UUID, CONSTRAINT ck CHECK(CAST(f AS STRING) != '11111111-1111-1111-1111-111111111111'));
    CREATE TABLE t5u (i INT PRIMARY KEY, f UUID UNIQUE);
]])

-- Check that FOREIGN KEY constraint can work with UUID.
test:do_catchsql_test(
    "uuid-5.1.1",
    [[
        INSERT INTO t5f SELECT (SELECT u from t2 LIMIT 1 OFFSET 1), (SELECT u from t2 LIMIT 1);
    ]], {
        1, "Failed to execute SQL statement: FOREIGN KEY constraint failed"
    })

test:do_execsql_test(
    "uuid-5.1.2",
    [[
        INSERT INTO t5f SELECT u, u from t2 LIMIT 1;
        SELECT * from t5f;
    ]], {
        uuid1, uuid1
    })

test:do_execsql_test(
    "uuid-5.1.3",
    [[
        INSERT INTO t5f SELECT (SELECT u from t2 LIMIT 1 OFFSET 1), (SELECT u from t2 LIMIT 1);
        SELECT * from t5f;
    ]], {
        uuid1, uuid1, uuid3, uuid1
    })

-- Check that CHECK constraint can work with UUID.
test:do_catchsql_test(
    "uuid-5.2.1",
    [[
        INSERT INTO t5c SELECT 1, u FROM t2 LIMIT 1;
    ]], {
        1, "Check constraint failed 'CK': CAST(f AS STRING) != '11111111-1111-1111-1111-111111111111'"
    })

test:do_execsql_test(
    "uuid-5.2.2",
    [[
        INSERT INTO t5c SELECT 2, u FROM t2 LIMIT 1 OFFSET 1;
        SELECT * from t5c;
    ]], {
        2, uuid3
    })

-- Check that UNIQUE constraint can work with UUID.
test:do_execsql_test(
    "uuid-5.3.1",
    [[
        INSERT INTO t5u SELECT 1, u FROM t2 LIMIT 1;
        SELECT * from t5u;
    ]], {
        1, uuid1
    })

test:do_catchsql_test(
    "uuid-5.3.2",
    [[
        INSERT INTO t5u SELECT 2, u FROM t2 LIMIT 1;
    ]], {
        1, 'Duplicate key exists in unique index "unique_unnamed_T5U_2" '..
        'in space "T5U" with old tuple - '..
        '[1, 11111111-1111-1111-1111-111111111111] and new tuple - '..
        '[2, 11111111-1111-1111-1111-111111111111]'
    })

-- Check that built-in functions work with UUIDs as intended.
test:do_catchsql_test(
    "uuid-6.1.1",
    [[
        SELECT ABS(u) from t2;
    ]], {
        1, "Inconsistent types: expected number got uuid('11111111-1111-1111-1111-111111111111')"
    })

test:do_catchsql_test(
    "uuid-6.1.2",
    [[
        SELECT AVG(u) from t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to number"
    })

test:do_execsql_test(
    "uuid-6.1.3",
    [[
        SELECT CHAR(u) from t2;
    ]], {
        "\0", "\0", "\0"
    })

test:do_execsql_test(
    "uuid-6.1.4",
    [[
        SELECT CHARACTER_LENGTH(u) from t2;
    ]], {
        36, 36, 36
    })

test:do_execsql_test(
    "uuid-6.1.5",
    [[
        SELECT CHAR_LENGTH(u) from t2;
    ]], {
        36, 36, 36
    })

test:do_execsql_test(
    "uuid-6.1.6",
    [[
        SELECT COALESCE(NULL, u, NULL, NULL) from t2;
    ]], {
        uuid1, uuid3, uuid2
    })

test:do_execsql_test(
    "uuid-6.1.7",
    [[
        SELECT COUNT(u) from t2;
    ]], {
        3
    })

test:do_execsql_test(
    "uuid-6.1.8",
    [[
        SELECT GREATEST((SELECT u FROM t2 LIMIT 1), (SELECT u FROM t2 LIMIT 1 OFFSET 1));
    ]], {
        uuid3
    })

test:do_execsql_test(
    "uuid-6.1.9",
    [[
        SELECT GROUP_CONCAT(u) from t2;
    ]], {
        "11111111-1111-1111-1111-111111111111,"..
        "11111111-3333-1111-1111-111111111111,"..
        "22222222-1111-1111-1111-111111111111"
    })

test:do_execsql_test(
    "uuid-6.1.10",
    [[
        SELECT HEX(u) from t2;
    ]], {
        "11111111111111111111111111111111",
        "11111111333311111111111111111111",
        "22222222111111111111111111111111"
    })

test:do_execsql_test(
    "uuid-6.1.11",
    [[
        SELECT IFNULL(u, NULL) from t2;
    ]], {
        uuid1, uuid3, uuid2
    })

test:do_execsql_test(
    "uuid-6.1.12",
    [[
        SELECT LEAST((SELECT u FROM t2 LIMIT 1), (SELECT u FROM t2 LIMIT 1 OFFSET 1));
    ]], {
        uuid1
    })

test:do_execsql_test(
    "uuid-6.1.13",
    [[
        SELECT LENGTH(u) from t2;
    ]], {
        36, 36, 36
    })

test:do_catchsql_test(
    "uuid-6.1.14",
    [[
        SELECT u LIKE 'a' from t2;
    ]], {
        1, "Inconsistent types: expected string got uuid('11111111-1111-1111-1111-111111111111')"
    })

test:do_execsql_test(
    "uuid-6.1.15",
    [[
        SELECT LIKELIHOOD(u, 0.5) from t2;
    ]], {
        uuid1, uuid3, uuid2
    })

test:do_execsql_test(
    "uuid-6.1.16",
    [[
        SELECT LIKELY(u) from t2;
    ]], {
        uuid1, uuid3, uuid2
    })

test:do_execsql_test(
    "uuid-6.1.17",
    [[
        SELECT LOWER(u) from t2;
    ]], {
        "11111111-1111-1111-1111-111111111111",
        "11111111-3333-1111-1111-111111111111",
        "22222222-1111-1111-1111-111111111111"
    })

test:do_execsql_test(
    "uuid-6.1.18",
    [[
        SELECT MAX(u) from t2;
    ]], {
        uuid2
    })

test:do_execsql_test(
    "uuid-6.1.19",
    [[
        SELECT MIN(u) from t2;
    ]], {
        uuid1
    })

test:do_execsql_test(
    "uuid-6.1.20",
    [[
        SELECT NULLIF(u, 1) from t2;
    ]], {
        uuid1, uuid3, uuid2
    })

test:do_catchsql_test(
    "uuid-6.1.21",
    [[
        SELECT POSITION(u, '1') from t2;
    ]], {
        1, "Inconsistent types: expected string or varbinary got uuid('11111111-1111-1111-1111-111111111111')"
    })

test:do_execsql_test(
    "uuid-6.1.22",
    [[
        SELECT RANDOMBLOB(u) from t2;
    ]], {
        "", "", ""
    })

test:do_execsql_test(
    "uuid-6.1.23",
    [[
        SELECT REPLACE(u, '1', '2') from t2;
    ]], {
        "22222222-2222-2222-2222-222222222222",
        "22222222-3333-2222-2222-222222222222",
        "22222222-2222-2222-2222-222222222222"
    })

test:do_catchsql_test(
    "uuid-6.1.24",
    [[
        SELECT ROUND(u) from t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to number"
    })

test:do_execsql_test(
    "uuid-6.1.25",
    [[
        SELECT SOUNDEX(u) from t2;
    ]], {
        "?000", "?000", "?000"
    })

test:do_execsql_test(
    "uuid-6.1.26",
    [[
        SELECT SUBSTR(u, 3, 3) from t2;
    ]], {
        "111", "111", "222"
    })

test:do_catchsql_test(
    "uuid-6.1.27",
    [[
        SELECT SUM(u) from t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to number"
    })

test:do_catchsql_test(
    "uuid-6.1.28",
    [[
        SELECT TOTAL(u) from t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to number"
    })

test:do_execsql_test(
    "uuid-6.1.29",
    [[
        SELECT TRIM(u) from t2;
    ]], {
        "11111111-1111-1111-1111-111111111111",
        "11111111-3333-1111-1111-111111111111",
        "22222222-1111-1111-1111-111111111111"
    })

test:do_execsql_test(
    "uuid-6.1.30",
    [[
        SELECT TYPEOF(u) from t2;
    ]], {
        "uuid", "uuid", "uuid"
    })

test:do_execsql_test(
    "uuid-6.1.31",
    [[
        SELECT UNICODE(u) from t2;
    ]], {
        49, 49, 50
    })

test:do_execsql_test(
    "uuid-6.1.32",
    [[
        SELECT UNLIKELY(u) from t2;
    ]], {
        uuid1, uuid3, uuid2
    })

test:do_execsql_test(
    "uuid-6.1.33",
    [[
        SELECT UPPER(u) from t2;
    ]], {
        "11111111-1111-1111-1111-111111111111",
        "11111111-3333-1111-1111-111111111111",
        "22222222-1111-1111-1111-111111111111"
    })

test:do_catchsql_test(
    "uuid-6.1.33",
    [[
        SELECT u || u from t2;
    ]], {
        1, "Inconsistent types: expected string or varbinary got uuid('11111111-1111-1111-1111-111111111111')"
    })

local func = {language = 'Lua', body = 'function(x) return type(x) end',
              returns = 'string', param_list = {'any'}, exports = {'SQL'}}
box.schema.func.create('RETURN_TYPE', func);

-- Check that Lua user-defined functions can accept UUID.
test:do_execsql_test(
    "uuid-6.2",
    [[
        SELECT RETURN_TYPE(u) FROM t2;
    ]], {
        "cdata", "cdata", "cdata"
    })

func = {language = 'Lua', returns = 'uuid', param_list = {}, exports = {'SQL'},
        body = 'function(x) return require("uuid").fromstr("11111111-1111-1111-1111-111111111111") end'}
box.schema.func.create('GET_UUID', func);

-- Check that Lua user-defined functions can return UUID.
test:do_execsql_test(
    "uuid-6.3",
    [[
        SELECT GET_UUID();
    ]], {
        uuid1
    })

func = {language = 'C', returns = 'boolean', param_list = {'any'}, exports = {'SQL'}}
box.schema.func.create("sql_uuid.is_uuid", func)

-- Check that C user-defined functions can accept UUID.
test:do_execsql_test(
    "uuid-6.4",
    [[
        SELECT "sql_uuid.is_uuid"(i), "sql_uuid.is_uuid"(u) FROM t1 LIMIT 1;
    ]], {
        false, true
    })

func = {language = 'C', returns = 'uuid', param_list = {}, exports = {'SQL'}}
box.schema.func.create("sql_uuid.ret_uuid", func)

-- Check that C user-defined functions can return UUID.
test:do_execsql_test(
    "uuid-6.5",
    [[
        SELECT "sql_uuid.ret_uuid"();
    ]], {
        uuid1
    })

-- Check that explicit cast from UUID to another types works as intended.
test:do_catchsql_test(
    "uuid-7.1.1",
    [[
        SELECT cast(u AS UNSIGNED) FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to unsigned"
    })

test:do_execsql_test(
    "uuid-7.1.2",
    [[
        SELECT cast(u AS STRING) FROM t2;
    ]], {
        "11111111-1111-1111-1111-111111111111",
        "11111111-3333-1111-1111-111111111111",
        "22222222-1111-1111-1111-111111111111"
    })

test:do_catchsql_test(
    "uuid-7.1.3",
    [[
        SELECT cast(u AS NUMBER) FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to number"
    })

test:do_catchsql_test(
    "uuid-7.1.4",
    [[
        SELECT cast(u AS DOUBLE) FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to double"
    })

test:do_catchsql_test(
    "uuid-7.1.5",
    [[
        SELECT cast(u AS INTEGER) FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to integer"
    })

test:do_catchsql_test(
    "uuid-7.1.6",
    [[
        SELECT cast(u AS BOOLEAN) FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to boolean"
    })

test:do_execsql_test(
    "uuid-7.1.7",
    [[
        SELECT hex(cast(u AS VARBINARY)) FROM t2;
    ]], {
        "11111111111111111111111111111111",
        "11111111333311111111111111111111",
        "22222222111111111111111111111111"
    })

test:do_execsql_test(
    "uuid-7.1.8",
    [[
        SELECT cast(u AS SCALAR) FROM t2;
    ]], {
        uuid1, uuid3, uuid2
    })

test:do_execsql_test(
    "uuid-7.1.9",
    [[
        SELECT cast(u AS UUID) FROM t2;
    ]], {
        uuid1, uuid3, uuid2
    })

-- Check that explicit cast from another types to UUID works as intended.
test:do_catchsql_test(
    "uuid-7.2.1",
    [[
        SELECT cast(1 AS UUID);
    ]], {
        1, "Type mismatch: can not convert integer(1) to uuid"
    })

test:do_execsql_test(
    "uuid-7.2.2",
    [[
        SELECT cast('11111111-1111-1111-1111-111111111111' AS UUID);
    ]], {
        uuid1
    })

test:do_catchsql_test(
    "uuid-7.2.3",
    [[
        SELECT cast('1' AS UUID);
    ]], {
        1, "Type mismatch: can not convert string('1') to uuid"
    })

test:do_catchsql_test(
    "uuid-7.2.4",
    [[
        SELECT cast(1.5 AS UUID);
    ]], {
        1, "Type mismatch: can not convert double(1.5) to uuid"
    })

test:do_catchsql_test(
    "uuid-7.2.5",
    [[
        SELECT cast(-1 AS UUID);
    ]], {
        1, "Type mismatch: can not convert integer(-1) to uuid"
    })

test:do_catchsql_test(
    "uuid-7.2.6",
    [[
        SELECT cast(true AS UUID);
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to uuid"
    })

test:do_execsql_test(
    "uuid-7.2.7",
    [[
        SELECT cast(x'11111111111111111111111111111111' AS UUID);
    ]], {
        uuid1
    })

test:do_catchsql_test(
    "uuid-7.2.8",
    [[
        SELECT cast(x'1234567890abcdef' as UUID) FROM t2 LIMIT 1;
    ]], {
        1, "Type mismatch: can not convert varbinary(x'1234567890ABCDEF') to uuid"
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

-- Check that implcit cast from UUID to another types works as intended.
test:do_catchsql_test(
    "uuid-8.1.1",
    [[
        INSERT INTO tu(u) SELECT u FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to unsigned"
    })

test:do_execsql_test(
    "uuid-8.1.2",
    [[
        INSERT INTO ts(s) SELECT u FROM t2;
        SELECT * FROM ts;
    ]], {
        1, "11111111-1111-1111-1111-111111111111",
        2, "11111111-3333-1111-1111-111111111111",
        3, "22222222-1111-1111-1111-111111111111"
    })

test:do_catchsql_test(
    "uuid-8.1.3",
    [[
        INSERT INTO tn(n) SELECT u FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to number"
    })

test:do_catchsql_test(
    "uuid-8.1.4",
    [[
        INSERT INTO td(d) SELECT u FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to double"
    })

test:do_catchsql_test(
    "uuid-8.1.5",
    [[
        INSERT INTO ti(i) SELECT u FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to integer"
    })

test:do_catchsql_test(
    "uuid-8.1.6",
    [[
        INSERT INTO tb(b) SELECT u FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to boolean"
    })

test:do_execsql_test(
    "uuid-8.1.7",
    [[
        INSERT INTO tv(v) SELECT u FROM t2;
        SELECT id, hex(v) FROM tv;
    ]], {
        1, "11111111111111111111111111111111",
        2, "11111111333311111111111111111111",
        3, "22222222111111111111111111111111"
    })

test:do_execsql_test(
    "uuid-8.1.8",
    [[
        INSERT INTO tsc(sc) SELECT u FROM t2;
        SELECT * FROM tsc;
    ]], {
        1, uuid1, 2, uuid3, 3, uuid2
    })

test:do_execsql_test(
    "uuid-8.1.9",
    [[
        INSERT INTO tuu(uu) SELECT u FROM t2;
        SELECT * FROM tuu;
    ]], {
        1, uuid1, 2, uuid3, 3, uuid2
    })

-- Check that implicit cast from another types to UUID works as intended.
test:do_catchsql_test(
    "uuid-8.2.1",
    [[
        INSERT INTO tsu VALUES ('1_unsigned', 1);
    ]], {
        1, "Type mismatch: can not convert integer(1) to uuid"
    })

test:do_execsql_test(
    "uuid-8.2.2",
    [[
        INSERT INTO tsu VALUES ('2_string_right', '11111111-1111-1111-1111-111111111111');
        SELECT * FROM tsu ORDER BY s DESC LIMIT 1;
    ]], {
        '2_string_right', uuid1
    })

test:do_catchsql_test(
    "uuid-8.2.3",
    [[
        INSERT INTO tsu VALUES ('3_string_wrong', '1');
    ]], {
        1, "Type mismatch: can not convert string('1') to uuid"
    })

test:do_catchsql_test(
    "uuid-8.2.4",
    [[
        INSERT INTO tsu VALUES ('4_double', 1.5);
    ]], {
        1, "Type mismatch: can not convert double(1.5) to uuid"
    })

test:do_catchsql_test(
    "uuid-8.2.5",
    [[
        INSERT INTO tsu VALUES ('5_integer', -1);
    ]], {
        1, "Type mismatch: can not convert integer(-1) to uuid"
    })

test:do_catchsql_test(
    "uuid-8.2.6",
    [[
        INSERT INTO tsu VALUES ('6_boolean', true);
    ]], {
        1, "Type mismatch: can not convert boolean(TRUE) to uuid"
    })

test:do_execsql_test(
    "uuid-8.2.7",
    [[
        INSERT INTO tsu SELECT '7_varbinary', x'11111111111111111111111111111111' FROM t2 LIMIT 1;
        SELECT * FROM tsu ORDER BY s DESC LIMIT 1;
    ]], {
        '7_varbinary', uuid1
    })

test:do_catchsql_test(
    "uuid-8.2.8",
    [[
        INSERT INTO tsu VALUES ('8_varbinary', x'1234567890abcdef');
    ]], {
        1, "Type mismatch: can not convert varbinary(x'1234567890ABCDEF') to uuid"
    })

test:execsql([[
    CREATE TABLE t9 (i INT PRIMARY KEY AUTOINCREMENT, u UUID);
    CREATE TABLE t9t (u UUID PRIMARY KEY);
    CREATE TRIGGER t AFTER INSERT ON t9 FOR EACH ROW BEGIN INSERT INTO t9t SELECT new.u; END;
]])

-- Check that trigger can work with UUID.
test:do_execsql_test(
    "uuid-9",
    [[
        INSERT INTO t9(u) SELECT * FROM t2;
        SELECT * FROM t9t;
    ]], {
        uuid1, uuid3, uuid2
    })

test:execsql([[
    CREATE TABLE t10 (i INT PRIMARY KEY AUTOINCREMENT, u UUID DEFAULT '11111111-1111-1111-1111-111111111111');
]])

-- Check that INSERT into UUID field works.
test:do_execsql_test(
    "uuid-10.1.1",
    [[
        INSERT INTO t10 VALUES (1, '22222222-1111-1111-1111-111111111111');
        SELECT * FROM t10 WHERE i = 1;
    ]], {
        1, uuid2
    })

test:do_execsql_test(
    "uuid-10.1.2",
    [[
        INSERT INTO t10 VALUES (2, x'22222222111111111111111111111111');
        SELECT * FROM t10 WHERE i = 2;
    ]], {
        2, uuid2
    })

test:do_execsql_test(
    "uuid-10.1.3",
    [[
        INSERT INTO t10(i) VALUES (3);
        SELECT * FROM t10 WHERE i = 3;
    ]], {
        3, uuid1
    })

test:do_execsql_test(
    "uuid-10.1.4",
    [[
        INSERT INTO t10 VALUES (4, NULL);
        SELECT * FROM t10 WHERE i = 4;
    ]], {
        4, ''
    })

-- Check that UPDATE of UUID field works.
test:do_execsql_test(
    "uuid-10.2.1",
    [[
        UPDATE t10 SET u = '11111111-3333-1111-1111-111111111111' WHERE i = 1;
        SELECT * FROM t10 WHERE i = 1;
    ]], {
        1, uuid3
    })

test:do_execsql_test(
    "uuid-10.2.2",
    [[
        UPDATE t10 SET u = x'11111111333311111111111111111111' WHERE i = 2;
        SELECT * FROM t10 WHERE i = 2;
    ]], {
        2, uuid3
    })

-- Check that JOIN by UUID field works.
test:do_execsql_test(
    "uuid-11.1",
    [[
        SELECT * FROM t1 JOIN t2 on t1.u = t2.u;
    ]], {
        1, uuid1, uuid1, 2, uuid2, uuid2, 3, uuid3, uuid3,
        4, uuid1, uuid1, 5, uuid1, uuid1, 6, uuid2, uuid2
    })

test:do_execsql_test(
    "uuid-11.2",
    [[
        SELECT * FROM t1 LEFT JOIN t2 on t1.u = t2.u;
    ]], {
        1, uuid1, uuid1, 2, uuid2, uuid2, 3, uuid3, uuid3,
        4, uuid1, uuid1, 5, uuid1, uuid1, 6, uuid2, uuid2
    })

test:do_execsql_test(
    "uuid-11.3",
    [[
        SELECT * FROM t1 INNER JOIN t2 on t1.u = t2.u;
    ]], {
        1, uuid1, uuid1, 2, uuid2, uuid2, 3, uuid3, uuid3,
        4, uuid1, uuid1, 5, uuid1, uuid1, 6, uuid2, uuid2
    })

-- Check that arithmetic operations work with UUIDs as intended.
test:do_catchsql_test(
    "uuid-12.1.1",
    [[
        SELECT -u FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to number"
    })

test:do_catchsql_test(
    "uuid-12.1.2",
    [[
        SELECT u + 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to number"
    })

test:do_catchsql_test(
    "uuid-12.1.3",
    [[
        SELECT u - 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to number"
    })

test:do_catchsql_test(
    "uuid-12.1.4",
    [[
        SELECT u * 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to number"
    })

test:do_catchsql_test(
    "uuid-12.1.5",
    [[
        SELECT u / 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to number"
    })

test:do_catchsql_test(
    "uuid-12.1.6",
    [[
        SELECT u % 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to number"
    })

-- Check that bitwise operations work with UUIDs as intended.
test:do_catchsql_test(
    "uuid-12.2.1",
    [[
        SELECT ~u FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to integer"
    })

test:do_catchsql_test(
    "uuid-12.2.2",
    [[
        SELECT u >> 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to integer"
    })

test:do_catchsql_test(
    "uuid-12.2.3",
    [[
        SELECT u << 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to integer"
    })

test:do_catchsql_test(
    "uuid-12.2.4",
    [[
        SELECT u | 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to integer"
    })

test:do_catchsql_test(
    "uuid-12.2.5",
    [[
        SELECT u & 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to integer"
    })

-- Check that logical operations work with UUIDs as intended.
test:do_catchsql_test(
    "uuid-12.3.1",
    [[
        SELECT NOT u FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to boolean"
    })

test:do_catchsql_test(
    "uuid-12.3.2",
    [[
        SELECT u AND true FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to boolean"
    })

test:do_catchsql_test(
    "uuid-12.3.3",
    [[
        SELECT u OR true FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to boolean"
    })

test:do_catchsql_test(
    "uuid-12.3.4",
    [[
        SELECT true AND u FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to boolean"
    })

test:do_catchsql_test(
    "uuid-12.3.5",
    [[
        SELECT true OR u FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to boolean"
    })

-- Check that comparison with UUID works as intended.
test:do_catchsql_test(
    "uuid-13.1.1",
    [[
        SELECT u > 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert integer(1) to uuid"
    })

test:do_execsql_test(
    "uuid-13.1.2",
    [[
        SELECT u > CAST('11111111-1111-1111-1111-111111111111' AS UUID) FROM t2;
    ]], {
        false, true, true
    })

test:do_catchsql_test(
    "uuid-13.1.3",
    [[
        SELECT u > '1' FROM t2;
    ]], {
        1, "Type mismatch: can not convert string('1') to uuid"
    })

test:do_catchsql_test(
    "uuid-13.1.4",
    [[
        SELECT u > 1.5 FROM t2;
    ]], {
        1, "Type mismatch: can not convert double(1.5) to uuid"
    })

test:do_catchsql_test(
    "uuid-13.1.5",
    [[
        SELECT u > -1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert integer(-1) to uuid"
    })

test:do_catchsql_test(
    "uuid-13.1.6",
    [[
        SELECT u > true FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to boolean"
    })

test:do_execsql_test(
    "uuid-13.1.7",
    [[
        SELECT u > CAST(x'11111111111111111111111111111111' AS UUID) FROM t2;
    ]], {
        false, true, true
    })

test:do_catchsql_test(
    "uuid-13.1.8",
    [[
        SELECT u > x'31' FROM t2;
    ]], {
        1, "Type mismatch: can not convert varbinary(x'31') to uuid"
    })

test:do_catchsql_test(
    "uuid-13.2.1",
    [[
        SELECT u = 1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert integer(1) to uuid"
    })

test:do_execsql_test(
    "uuid-13.2.2",
    [[
        SELECT u = CAST('11111111-1111-1111-1111-111111111111' AS UUID) FROM t2;
    ]], {
        true, false, false
    })

test:do_catchsql_test(
    "uuid-13.2.3",
    [[
        SELECT u = '1' FROM t2;
    ]], {
        1, "Type mismatch: can not convert string('1') to uuid"
    })

test:do_catchsql_test(
    "uuid-13.2.4",
    [[
        SELECT u = 1.5 FROM t2;
    ]], {
        1, "Type mismatch: can not convert double(1.5) to uuid"
    })

test:do_catchsql_test(
    "uuid-13.2.5",
    [[
        SELECT u = -1 FROM t2;
    ]], {
        1, "Type mismatch: can not convert integer(-1) to uuid"
    })

test:do_catchsql_test(
    "uuid-13.2.6",
    [[
        SELECT u = true FROM t2;
    ]], {
        1, "Type mismatch: can not convert uuid('11111111-1111-1111-1111-111111111111') to boolean"
    })

test:do_execsql_test(
    "uuid-13.2.7",
    [[
        SELECT u = CAST(x'11111111111111111111111111111111' AS UUID) FROM t2;
    ]], {
        true, false, false
    })

test:do_catchsql_test(
    "uuid-13.2.8",
    [[
        SELECT u = x'31' FROM t2;
    ]], {
        1, "Type mismatch: can not convert varbinary(x'31') to uuid"
    })

test:execsql([[
    CREATE TABLE t14 (s SCALAR PRIMARY KEY);
]])

-- Check that SCALAR field can contain UUID and use it in index.
test:do_execsql_test(
    "uuid-14",
    [[
        INSERT INTO t14 VALUES (1), (true), (1.5), (-1);
        INSERT INTO t14 VALUES (x'11111111111111111111111111111111');
        INSERT INTO t14 VALUES (CAST(x'11111111111111111111111111111111' AS UUID));
        INSERT INTO t14 VALUES ('11111111-1111-1111-1111-111111111111');
        SELECT typeof(s) FROM t14;
    ]], {
        "boolean", "integer", "integer", "double", "string", "varbinary", "uuid"
    })

local s = box.schema.space.create('T15', {format={{'I', 'integer'}, {'M', 'map'}, {'A', 'array'}}})
s:create_index('i15')
s:insert({1, {['1'] = 1, ['2'] = 2, ['3']= 3}, {1,2,3}})
s:insert({2, {['1'] = 1, ['2'] = 2}, {1,2,3,4}})
s:insert({3, {['1'] = 1}, {1,2,3,4,5}})

-- Make sure that addition of UUID does not change behaviour of MAP and ARRAY.
-- Currently it works wrong, but there should not segmentation faults.
test:do_execsql_test(
    "uuid-15.1",
    [[
        SELECT i FROM t15 ORDER BY m;
    ]], {
        3,2,1
    })

test:do_execsql_test(
    "uuid-15.2",
    [[
        SELECT i FROM t15 ORDER BY a;
    ]], {
        3,2,1
    })

-- Check function uuid().
test:do_execsql_test(
    "uuid-16.1",
    [[
        SELECT typeof(uuid());
    ]], {
        "uuid"
    })

test:do_execsql_test(
    "uuid-16.2",
    [[
        SELECT typeof(uuid(4));
    ]], {
        "uuid"
    })

test:do_catchsql_test(
    "uuid-16.3",
    [[
        SELECT uuid(1);
    ]], {
        1, "Function UUID does not support versions other than 4"
    })

test:do_catchsql_test(
    "uuid-16.4",
    [[
        SELECT uuid('asd');
    ]], {
        1, "Type mismatch: can not convert string('asd') to integer"
    })

test:do_catchsql_test(
    "uuid-16.5",
    [[
        SELECT uuid(4, 5);
    ]], {
        1, "Wrong number of arguments is passed to UUID(): expected one or zero, got 2"
    })

-- Make sure the uuid() function generates a new UUID each time when called.
test:do_execsql_test(
    "uuid-16.6",
    [[
        SELECT uuid() != uuid();
    ]], {
        true
    })

-- Make sure STRING of wrong length cannot be cast to UUID.
test:do_catchsql_test(
    "uuid-17.1",
    [[
        SELECT CAST('11111111-1111-1111-1111-111111111111111222222222' AS UUID);
    ]], {
        1, "Type mismatch: can not convert string('11111111-1111-1111-1111-111111111111111222222222') to uuid"
    })

test:do_catchsql_test(
    "uuid-17.2",
    [[
        SELECT CAST('11111111-1111-1111-1111-11111' AS UUID);
    ]], {
        1, "Type mismatch: can not convert string('11111111-1111-1111-1111-11111') to uuid"
    })

test:execsql([[
    DROP TRIGGER t;
    DROP VIEW v;
    DROP TABLE t15;
    DROP TABLE t14;
    DROP TABLE t10;
    DROP TABLE t9t;
    DROP TABLE t9;
    DROP TABLE tsu;
    DROP TABLE tuu;
    DROP TABLE tsc;
    DROP TABLE tv;
    DROP TABLE tb;
    DROP TABLE ti;
    DROP TABLE td;
    DROP TABLE tn;
    DROP TABLE ts;
    DROP TABLE tu;
    DROP TABLE t5u;
    DROP TABLE t5c;
    DROP TABLE t5f;
    DROP TABLE t2;
    DROP TABLE t1;
]])

test:finish_test()
