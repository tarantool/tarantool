#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(47)

--!./tcltestrunner.lua
-- 2008 January 5
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- $Id: minmax3.test,v 1.5 2008/07/12 14:52:20 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Do not use a codec for tests in this file, as the database file is
-- manipulated directly using tcl scripts (using the [hexio_write] command).
--
-- do_not_use_codec
-- Do an SQL statement.  Append the search count to the end of the result.
--

local function count(sql)
    local sql_search_count = box.stat.sql().sql_search_count
    local r = test:execsql(sql)
    table.insert(r, box.stat.sql().sql_search_count - sql_search_count)
    return r
end

-- This procedure sets the value of the file-format in file 'test.db'
-- to $newval. Also, the schema cookie is incremented.
--
--local function set_file_format(newval)
--    X(35, "X!cmd", [=[["hexio_write","test.db","44",[["hexio_render_int32",["newval"]]]]]=])
--    schemacookie = X(37, "X!cmd", [=[["hexio_get_int",[["hexio_read","test.db","40","4"]]]]=])
--    schemacookie = schemacookie + 1
--    X(38, "X!cmd", [=[["hexio_write","test.db","40",[["hexio_render_int32",["schemacookie"]]]]]=])
--    return ""
--end


test:do_test(
    "minmax3-1.0",
    function()
        test:execsql [[
            CREATE TABLE t1(id  INT primary key, x TEXT, y TEXT, z TEXT)
        ]]
        -- db close
        -- set_file_format 4
        -- sql db test.db
        return test:execsql [[
            START TRANSACTION;
            INSERT INTO t1 VALUES(1, '1', 'I',   'one');
            INSERT INTO t1 VALUES(2, '2', 'IV',  'four');
            INSERT INTO t1 VALUES(3, '2', NULL,  'three');
            INSERT INTO t1 VALUES(4, '2', 'II',  'two');
            INSERT INTO t1 VALUES(5, '2', 'V',   'five');
            INSERT INTO t1 VALUES(6, '3', 'VI',  'six');
            COMMIT;
--            PRAGMA automatic_index='OFF';
        ]]
    end, {
        -- <minmax3-1.0>

        -- </minmax3-1.0>
    })

test:do_test(
    "minmax3-1.1.1",
    function()
        -- Linear scan.
        return count(" SELECT max(y) FROM t1 WHERE x = '2'; ")
    end, {
        -- <minmax3-1.1.1>
        "V", 5
        -- </minmax3-1.1.1>
    })

-- Tarantool: On table without rowid OP_Seek is not emitted
-- (used to position by rowid). So, number of searches reduce
-- Update expected result: 9 -> 4.
test:do_test(
    "minmax3-1.1.2",
    function()
        -- Index optimizes the WHERE x='2' constraint.
        test:execsql " CREATE INDEX i1 ON t1(x) "
        return count(" SELECT max(y) FROM t1 WHERE x = '2'; ")
    end, {
        -- <minmax3-1.1.2>
        "V", 4
        -- </minmax3-1.1.2>
    })

test:do_test(
    "minmax3-1.1.3",
    function()
        -- Index optimizes the WHERE x='2' constraint and the MAX(y).
        test:execsql " CREATE INDEX i2 ON t1(x,y) "
        return count(" SELECT max(y) FROM t1 WHERE x = '2'; ")
    end, {
        -- <minmax3-1.1.3>
        "V", 1
        -- </minmax3-1.1.3>
    })

test:do_test(
    "minmax3-1.1.4",
    function()
        -- Index optimizes the WHERE x='2' constraint and the MAX(y).
        test:execsql " DROP INDEX i2 ON t1; CREATE INDEX i2 ON t1(x, y DESC) "
        return count(" SELECT max(y) FROM t1 WHERE x = '2'; ")
    end, {
        -- <minmax3-1.1.4>
        "V", 1
        -- </minmax3-1.1.4>
    })

test:do_test(
    "minmax3-1.1.5",
    function()
        return count(" SELECT max(y) FROM t1 WHERE x = '2' AND y != 'V'; ")
    end, {
        -- <minmax3-1.1.5>
        "IV", 2
        -- </minmax3-1.1.5>
    })

test:do_test(
    "minmax3-1.1.6",
    function()
        return count(" SELECT max(y) FROM t1 WHERE x = '2' AND y < 'V'; ")
    end, {
        -- <minmax3-1.1.6>
        "IV", 1
        -- </minmax3-1.1.6>
    })

-- Tarantool: see comment to minmax3-1.1.2. Change 4 -> 2.
test:do_test(
    "minmax3-1.1.6",
    function()
        return count(" SELECT max(y) FROM t1 WHERE x = '2' AND z != 'five'; ")
    end, {
        -- <minmax3-1.1.6>
        "IV", 2
        -- </minmax3-1.1.6>
    })

test:do_test(
    "minmax3-1.2.1",
    function()
        -- Linear scan of t1.
        test:execsql " DROP INDEX i1 ON t1; DROP INDEX i2 ON t1;"
        return count(" SELECT min(y) FROM t1 WHERE x = '2'; ")
    end, {
        -- <minmax3-1.2.1>
        "II", 5
        -- </minmax3-1.2.1>
    })

-- Tarantool: see comment to minmax3-1.2.2. Change 4 -> 2.
test:do_test(
    "minmax3-1.2.2",
    function()
        -- Index i1 optimizes the WHERE x='2' constraint.
        test:execsql " CREATE INDEX i1 ON t1(x) "
        return count(" SELECT min(y) FROM t1 WHERE x = '2'; ")
    end, {
        -- <minmax3-1.2.2>
        "II", 5
        -- </minmax3-1.2.2>
    })

test:do_test(
    "minmax3-1.2.3",
    function()
        -- Index i2 optimizes the WHERE x='2' constraint and the min(y).
        test:execsql " CREATE INDEX i2 ON t1(x,y) "
        return count(" SELECT min(y) FROM t1 WHERE x = '2'; ")
    end, {
        -- <minmax3-1.2.3>
        "II", 1
        -- </minmax3-1.2.3>
    })

test:do_test(
    "minmax3-1.2.4",
    function()
        -- Index optimizes the WHERE x='2' constraint and the MAX(y).
        test:execsql " DROP INDEX i2 ON t1; CREATE INDEX i2 ON t1(x, y DESC) "
        return count(" SELECT min(y) FROM t1 WHERE x = '2'; ")
    end, {
        -- <minmax3-1.2.4>
        "II", 1
        -- </minmax3-1.2.4>
    })

test:do_test(
    "minmax3-1.3.1",
    function()
        -- Linear scan
        test:execsql " DROP INDEX i1 ON t1; DROP INDEX i2 ON t1;"
        return count(" SELECT min(y) FROM t1; ")
    end, {
        -- <minmax3-1.3.1>
        "I", 5
        -- </minmax3-1.3.1>
    })

test:do_test(
    "minmax3-1.3.2",
    function()
        -- Index i1 optimizes the min(y)
        test:execsql " CREATE INDEX i1 ON t1(y) "
        return count(" SELECT min(y) FROM t1; ")
    end, {
        -- <minmax3-1.3.2>
        "I", 1
        -- </minmax3-1.3.2>
    })

test:do_test(
    "minmax3-1.3.3",
    function()
        -- Index i1 optimizes the min(y)
        test:execsql " DROP INDEX i1 ON t1; CREATE INDEX i1 ON t1(y DESC) "
        return count(" SELECT min(y) FROM t1; ")
    end, {
        -- <minmax3-1.3.3>
        "I", 1
        -- </minmax3-1.3.3>
    })

test:do_test(
    "minmax3-1.4.1",
    function()
        -- Linear scan
        test:execsql " DROP INDEX i1 ON t1;"
        return count(" SELECT max(y) FROM t1; ")
    end, {
        -- <minmax3-1.4.1>
        "VI", 5
        -- </minmax3-1.4.1>
    })

test:do_test(
    "minmax3-1.4.2",
    function()
        -- Index i1 optimizes the max(y)
        test:execsql " CREATE INDEX i1 ON t1(y) "
        return count(" SELECT max(y) FROM t1; ")
    end, {
        -- <minmax3-1.4.2>
        "VI", 0
        -- </minmax3-1.4.2>
    })

test:do_test(
    "minmax3-1.4.3",
    function()
        -- Index i1 optimizes the max(y)
        test:execsql " DROP INDEX i1 ON t1; CREATE INDEX i1 ON t1(y DESC) "
        test:execsql " SELECT y from t1"
        return count(" SELECT max(y) FROM t1; ")
    end, {
        -- <minmax3-1.4.3>
        "VI", 0
        -- </minmax3-1.4.3>
    })

test:do_execsql_test(
    "minmax3-1.4.4",
    [[
        DROP INDEX i1 ON t1;
    ]], {
        -- <minmax3-1.4.4>

        -- </minmax3-1.4.4>
    })

test:do_execsql_test(
    "minmax3-2.1",
    [[
        CREATE TABLE t2(id  INT primary key, a INT , b INT );
        CREATE INDEX i3 ON t2(a, b);
        INSERT INTO t2 VALUES(1, 1, NULL);
        INSERT INTO t2 VALUES(2, 1, 1);
        INSERT INTO t2 VALUES(3, 1, 2);
        INSERT INTO t2 VALUES(4, 1, 3);
        INSERT INTO t2 VALUES(5, 2, NULL);
        INSERT INTO t2 VALUES(6, 2, 1);
        INSERT INTO t2 VALUES(7, 2, 2);
        INSERT INTO t2 VALUES(8, 2, 3);
        INSERT INTO t2 VALUES(9, 3, 1);
        INSERT INTO t2 VALUES(10, 3, 2);
        INSERT INTO t2 VALUES(11, 3, 3);
    ]], {
        -- <minmax3-2.1>

        -- </minmax3-2.1>
    })

test:do_execsql_test(
    "minmax3-2.2",
    [[
        SELECT min(b) FROM t2 WHERE a = 1;
    ]], {
        -- <minmax3-2.2>
        1
        -- </minmax3-2.2>
    })

test:do_execsql_test(
    "minmax3-2.3",
    [[
        SELECT min(b) FROM t2 WHERE a = 1 AND b>1;
    ]], {
        -- <minmax3-2.3>
        2
        -- </minmax3-2.3>
    })

test:do_execsql_test(
    "minmax3-2.4",
    [[
        SELECT min(b) FROM t2 WHERE a = 1 AND b>-1;
    ]], {
        -- <minmax3-2.4>
        1
        -- </minmax3-2.4>
    })

test:do_execsql_test(
    "minmax3-2.5",
    [[
        SELECT min(b) FROM t2 WHERE a = 1;
    ]], {
        -- <minmax3-2.5>
        1
        -- </minmax3-2.5>
    })

test:do_execsql_test(
    "minmax3-2.6",
    [[
        SELECT min(b) FROM t2 WHERE a = 1 AND b<2;
    ]], {
        -- <minmax3-2.6>
        1
        -- </minmax3-2.6>
    })

test:do_execsql_test(
    "minmax3-2.7",
    [[
        SELECT min(b) FROM t2 WHERE a = 1 AND b<1;
    ]], {
        -- <minmax3-2.7>
        ""
        -- </minmax3-2.7>
    })

test:do_execsql_test(
    "minmax3-2.8",
    [[
        SELECT min(b) FROM t2 WHERE a = 3 AND b<1;
    ]], {
        -- <minmax3-2.8>
        ""
        -- </minmax3-2.8>
    })

test:do_execsql_test(
    "minmax3-3.1",
    [[
        DROP TABLE t2;
        CREATE TABLE t2(id  INT primary key, a INT , b INT );
        CREATE INDEX i3 ON t2(a, b DESC);
        INSERT INTO t2 VALUES(1, 1, NULL);
        INSERT INTO t2 VALUES(2, 1, 1);
        INSERT INTO t2 VALUES(3, 1, 2);
        INSERT INTO t2 VALUES(4, 1, 3);
        INSERT INTO t2 VALUES(5, 2, NULL);
        INSERT INTO t2 VALUES(6, 2, 1);
        INSERT INTO t2 VALUES(7, 2, 2);
        INSERT INTO t2 VALUES(8, 2, 3);
        INSERT INTO t2 VALUES(9, 3, 1);
        INSERT INTO t2 VALUES(10, 3, 2);
        INSERT INTO t2 VALUES(11, 3, 3);
    ]], {
        -- <minmax3-3.1>

        -- </minmax3-3.1>
    })

test:do_execsql_test(
    "minmax3-3.2",
    [[
        SELECT min(b) FROM t2 WHERE a = 1;
    ]], {
        -- <minmax3-3.2>
        1
        -- </minmax3-3.2>
    })

test:do_execsql_test(
    "minmax3-3.3",
    [[
        SELECT min(b) FROM t2 WHERE a = 1 AND b>1;
    ]], {
        -- <minmax3-3.3>
        2
        -- </minmax3-3.3>
    })

test:do_execsql_test(
    "minmax3-3.4",
    [[
        SELECT min(b) FROM t2 WHERE a = 1 AND b>-1;
    ]], {
        -- <minmax3-3.4>
        1
        -- </minmax3-3.4>
    })

test:do_execsql_test(
    "minmax3-3.5",
    [[
        SELECT min(b) FROM t2 WHERE a = 1;
    ]], {
        -- <minmax3-3.5>
        1
        -- </minmax3-3.5>
    })

test:do_execsql_test(
    "minmax3-3.6",
    [[
        SELECT min(b) FROM t2 WHERE a = 1 AND b<2;
    ]], {
        -- <minmax3-3.6>
        1
        -- </minmax3-3.6>
    })

test:do_execsql_test(
    "minmax3-3.7",
    [[
        SELECT min(b) FROM t2 WHERE a = 1 AND b<1;
    ]], {
        -- <minmax3-3.7>
        ""
        -- </minmax3-3.7>
    })

test:do_execsql_test(
    "minmax3-3.8",
    [[
        SELECT min(b) FROM t2 WHERE a = 3 AND b<1;
    ]], {
        -- <minmax3-3.8>
        ""
        -- </minmax3-3.8>
    })

test:do_execsql_test(
    "minmax3-4.1",
    [[
        CREATE TABLE t4(x TEXT primary key);
        INSERT INTO t4 VALUES('abc');
        INSERT INTO t4 VALUES('BCD');
        SELECT max(x) FROM t4;
    ]], {
        -- <minmax3-4.1>
        "abc"
        -- </minmax3-4.1>
    })

test:do_execsql_test(
    "minmax3-4.2",
    [[
        SELECT max(x COLLATE "unicode_ci") FROM t4;
    ]], {
        -- <minmax3-4.2>
        "BCD"
        -- </minmax3-4.2>
    })

test:do_execsql_test(
    "minmax3-4.3",
    [[
        SELECT max(x), max(x COLLATE "unicode_ci") FROM t4;
    ]], {
        -- <minmax3-4.3>
        "abc", "BCD"
        -- </minmax3-4.3>
    })

test:do_execsql_test(
    "minmax3-4.4",
    [[
        SELECT max(x COLLATE "binary"), max(x COLLATE "unicode_ci") FROM t4;
    ]], {
        -- <minmax3-4.4>
        "abc", "BCD"
        -- </minmax3-4.4>
    })

test:do_execsql_test(
    "minmax3-4.5",
    [[
        --SELECT max(x COLLATE nocase), max(x COLLATE rtrim) FROM t4;
        SELECT max(x COLLATE "unicode_ci") FROM t4;
    ]], {
        -- <minmax3-4.5>
        --"BCD", "abc"
        "BCD"
        -- </minmax3-4.5>
    })

test:do_execsql_test(
    "minmax3-4.6",
    [[
        SELECT max(x COLLATE "unicode_ci"), max(x) FROM t4;
    ]], {
        -- <minmax3-4.6>
        "BCD", "abc"
        -- </minmax3-4.6>
    })

test:do_execsql_test(
    "minmax3-4.10",
    [[
        SELECT min(x) FROM t4;
    ]], {
        -- <minmax3-4.10>
        "BCD"
        -- </minmax3-4.10>
    })

test:do_execsql_test(
    "minmax3-4.11",
    [[
        SELECT min(x COLLATE "unicode_ci") FROM t4;
    ]], {
        -- <minmax3-4.11>
        "abc"
        -- </minmax3-4.11>
    })

test:do_execsql_test(
    "minmax3-4.12",
    [[
        SELECT min(x), min(x COLLATE "unicode_ci") FROM t4;
    ]], {
        -- <minmax3-4.12>
        "BCD", "abc"
        -- </minmax3-4.12>
    })

test:do_execsql_test(
    "minmax3-4.13",
    [[
        SELECT min(x COLLATE "binary"), min(x COLLATE "unicode_ci") FROM t4;
    ]], {
        -- <minmax3-4.13>
        "BCD", "abc"
        -- </minmax3-4.13>
    })

test:do_execsql_test(
    "minmax3-4.14",
    [[
        --SELECT min(x COLLATE nocase), min(x COLLATE rtrim) FROM t4;
        SELECT min(x COLLATE "unicode_ci") FROM t4;
    ]], {
        -- <minmax3-4.14>
        --"abc", "BCD"
        "abc"
        -- </minmax3-4.14>
    })

test:do_execsql_test(
    "minmax3-4.15",
    [[
        SELECT min(x COLLATE "unicode_ci"), min(x) FROM t4;
    ]], {
        -- <minmax3-4.15>
        "abc", "BCD"
        -- </minmax3-4.15>
    })



test:finish_test()
