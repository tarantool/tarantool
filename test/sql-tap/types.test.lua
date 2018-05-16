#!/usr/bin/env tarantool
test = require("sqltester")
NULL = require('msgpack').NULL
test:plan(51)

--!./tcltestrunner.lua
-- 2001 September 15
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library. Specfically
-- it tests that the different storage classes (integer, real, text etc.)
-- all work correctly.
--
-- $Id: types.test,v 1.20 2009/06/29 06:00:37 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Tests in this file are organized roughly as follows:
--
-- types-1.*.*: Test that values are stored using the expected storage
--              classes when various forms of literals are inserted into
--              columns with different affinities.
-- types-1.1.*: INSERT INTO <table> VALUES(...)
-- types-1.2.*: INSERT INTO <table> SELECT...
-- types-1.3.*: UPDATE <table> SET...
--
-- types-2.*.*: Check that values can be stored and retrieving using the
--              various storage classes.
-- types-2.1.*: INTEGER
-- types-2.2.*: REAL
-- types-2.3.*: NULL
-- types-2.4.*: TEXT
-- types-2.5.*: Records with a few different storage classes.
--
-- types-3.*: Test that the '=' operator respects manifest types.
--
-- Disable encryption on the database for this test.
--db("close")
--DB = X(44, "X!expr", [=[[["sqlite3","db","test.db"],["sqlite3_connection_pointer","db"]]]=])
--sqlite3_rekey $DB {}
-- Create a table with one column for each type of affinity
test:do_execsql_test(
    "types-1.1.0",
    [[
        CREATE TABLE t1(id primary key, i integer, n numeric, t text, o blob);
    ]], {
        -- <types-1.1.0>
        
        -- </types-1.1.0>
    })

-- Each element of the following list represents one test case.
--
-- The first value of each sub-list is an SQL literal. The following
-- four value are the storage classes that would be used if the
-- literal were inserted into a column with affinity INTEGER, NUMERIC, TEXT
-- or NONE, respectively.
local values = {
      {1, '5.0', {"integer", "integer", "text", "real"}},
      {2, '5.1', {"real", "real", "text", "real"}},
      {3, '5', {"integer", "integer", "text", "integer"}},
      {4, "'5.0'", {"integer", "integer", "text", "text"}},
      {5, "'5.1'", {"real", "real", "text", "text"}},
      {6, "'-5.0'", {"integer", "integer", "text", "text"}},
      {7, "'-5.0'", {"integer", "integer", "text", "text"}},
      {8, "'5'", {"integer", "integer", "text", "text"}},
      {9, "'abc'", {"text", "text", "text", "text"}},
      {10, 'NULL', {"null", "null", "null", "null"}},
      {11, "X'00'",  {"blob", "blob", "blob", "blob"}},
}


-- This code tests that the storage classes specified above (in the $values
-- table) are correctly assigned when values are inserted using a statement
-- of the form:
--
-- INSERT INTO <table> VALUE(<values>);

for _, val in ipairs(values) do
    local tnum = val[1]
    local lit = test.lindex(val, 1)
    test:execsql "DELETE FROM t1;"
    test:execsql(string.format("INSERT INTO t1 VALUES(1, %s, %s, %s, %s);", lit, lit, lit, lit))
    test:do_execsql_test(
        "types-1.1."..tnum,
        [[
            SELECT typeof(i), typeof(n), typeof(t), typeof(o) FROM t1;
        ]], val[3])
end
-- This code tests that the storage classes specified above (in the $values
-- table) are correctly assigned when values are inserted using a statement
-- of the form:
--
-- INSERT INTO t1 SELECT ....
--
for _, val in ipairs(values) do
    local tnum = val[1]
    local lit = test.lindex(val, 1)
    test:execsql "DELETE FROM t1;"
    test:execsql(string.format("INSERT INTO t1 SELECT 1, %s, %s, %s, %s;", lit, lit, lit, lit))
    test:do_execsql_test(
        "types-1.2."..tnum,
        [[
            SELECT typeof(i), typeof(n), typeof(t), typeof(o) FROM t1;
        ]], val[3])

end
-- This code tests that the storage classes specified above (in the $values
-- table) are correctly assigned when values are inserted using a statement
-- of the form:
--
-- UPDATE <table> SET <column> = <value>;
--
for _, val in ipairs(values) do
    local tnum = val[1]
    local lit = test.lindex(val, 1)
    test:execsql(string.format("UPDATE t1 SET id = 1, i = %s, n = %s, t = %s, o = %s;", lit, lit, lit, lit))
    test:do_execsql_test(
        "types-1.3."..tnum,
        [[
            SELECT typeof(i), typeof(n), typeof(t), typeof(o) FROM t1;
        ]], val[3])

    tnum = tnum + 1
end
test:execsql [[
    DROP TABLE t1;
]]
---- Open the table with root-page $rootpage at the btree
---- level. Return a list that is the length of each record
---- in the table, in the tables default scanning order.
--local function record_sizes(rootpage)
--    bt = X(147, "X!cmd", [=[["btree_open","test.db","10"]]=])
--    X(147, "X!cmd", [=[["btree_begin_transaction",["bt"]]]=])
--    c = X(149, "X!cmd", [=[["btree_cursor",["bt"],["rootpage"],"0"]]=])
--    X(149, "X!cmd", [=[["btree_first",["c"]]]=])
--    while 1
-- do
--        table.insert(res,X(153, "X!cmd", [=[["btree_payload_size",["c"]]]=]))
--        if X(154, "X!cmd", [=[["btree_next",["c"]]]=])
-- then
--            break
--        end
--    end
--    X(154, "X!cmd", [=[["btree_close_cursor",["c"]]]=])
--    X(155, "X!cmd", [=[["btree_close",["bt"]]]=])
--    return res
--end

-- Create a table and insert some 1-byte integers. Make sure they
-- can be read back OK. These should be 3 byte records.
test:do_execsql_test(
    "types-2.1.1",
    [[
        CREATE TABLE t1(id primary key, a integer);
        INSERT INTO t1 VALUES(1, 0);
        INSERT INTO t1 VALUES(2, 120);
        INSERT INTO t1 VALUES(3, -120);
    ]], {
        -- <types-2.1.1>
        
        -- </types-2.1.1>
    })

test:do_execsql_test(
    "types-2.1.2",
    [[
        SELECT a FROM t1;
    ]], {
        -- <types-2.1.2>
        0, 120, -120
        -- </types-2.1.2>
    })

-- Try some 2-byte integers (4 byte records)
test:do_execsql_test(
    "types-2.1.3",
    [[
        INSERT INTO t1 VALUES(4, 30000);
        INSERT INTO t1 VALUES(5, -30000);
    ]], {
        -- <types-2.1.3>
        
        -- </types-2.1.3>
    })

test:do_execsql_test(
    "types-2.1.4",
    [[
        SELECT a FROM t1;
    ]], {
        -- <types-2.1.4>
        0, 120, -120, 30000, -30000
        -- </types-2.1.4>
    })

-- 4-byte integers (6 byte records)
test:do_execsql_test(
    "types-2.1.5",
    [[
        INSERT INTO t1 VALUES(6, 2100000000);
        INSERT INTO t1 VALUES(7, -2100000000);
    ]], {
        -- <types-2.1.5>
        
        -- </types-2.1.5>
    })

test:do_execsql_test(
    "types-2.1.6",
    [[
        SELECT a FROM t1;
    ]], {
        -- <types-2.1.6>
        0, 120, -120, 30000, -30000, 2100000000, -2100000000
        -- </types-2.1.6>
    })

-- 8-byte integers (10 byte records)
test:do_execsql_test(
    "types-2.1.7",
    [[
        INSERT INTO t1 VALUES(8, 9000000*1000000*1000000);
        INSERT INTO t1 VALUES(9, -9000000*1000000*1000000);
    ]], {
        -- <types-2.1.7>
        
        -- </types-2.1.7>
    })

test:do_execsql_test(
    "types-2.1.8",
    [[
        SELECT a FROM t1;
    ]], {
        -- <types-2.1.8>
        0, 120, -120, 30000, -30000, 2100000000, -2100000000, 9000000000000000000LL, -9000000000000000000LL
        -- </types-2.1.8>
    })
-- # Check that all the record sizes are as we expected.
-- ifcapable legacyformat {
--   do_test types-2.1.9 {
--     set root [db eval {select rootpage from sqlite_master where name = 't1'}]
--     record_sizes $root
--   } {3 3 3 4 4 6 6 10 10}
-- } else {
--   do_test types-2.1.9 {
--     set root [db eval {select rootpage from sqlite_master where name = 't1'}]
--     record_sizes $root
--   } {2 3 3 4 4 6 6 10 10}
-- }
-- Insert some reals. These should be 10 byte records.
test:do_execsql_test(
    "types-2.2.1",
    [[
        CREATE TABLE t2(id primary key, a float);
        INSERT INTO t2 VALUES(1, 0.0);
        INSERT INTO t2 VALUES(2, 12345.678);
        INSERT INTO t2 VALUES(3, -12345.678);
    ]], {
        -- <types-2.2.1>
        
        -- </types-2.2.1>
    })

test:do_execsql_test(
    "types-2.2.2",
    [[
        SELECT a FROM t2;
    ]], {
        -- <types-2.2.2>
        0.0, 12345.678, -12345.678
        -- </types-2.2.2>
    })

-- # Check that all the record sizes are as we expected.
-- ifcapable legacyformat {
--   do_test types-2.2.3 {
--     set root [db eval {select rootpage from sqlite_master where name = 't2'}]
--     record_sizes $root
--   } {3 10 10}
-- } else {
--   do_test types-2.2.3 {
--     set root [db eval {select rootpage from sqlite_master where name = 't2'}]
--     record_sizes $root
--   } {2 10 10}
-- }
-- Insert a NULL. This should be a two byte record.
test:do_execsql_test(
    "types-2.3.1",
    [[
        CREATE TABLE t3(id primary key, a nullvalue);
        INSERT INTO t3 VALUES(1, NULL);
    ]], {
        -- <types-2.3.1>
        
        -- </types-2.3.1>
    })

test:do_execsql_test(
    "types-2.3.2",
    [[
        SELECT a IS NULL FROM t3;
    ]], {
        -- <types-2.3.2>
        1
        -- </types-2.3.2>
    })

-- # Check that all the record sizes are as we expected.
-- do_test types-2.3.3 {
--   set root [db eval {select rootpage from sqlite_master where name = 't3'}]
--   record_sizes $root
-- } {2}
-- Insert a couple of strings.
local string10 = "abcdefghij"
local string500 = string.rep(string10, 50)
-- MUST_WORK_TEST disabled because in is somewhy very slow
local string500000
if 0>0 then
    string500000 = string.rep(string10, 50000)
else
    string500000 = string.rep(string10, 5000)
end

test:do_test(
    "types-2.4.1",
    function()
        return test:execsql(string.format([[
            CREATE TABLE t4(id primary key, a string);
            INSERT INTO t4 VALUES(1, '%s');
            INSERT INTO t4 VALUES(2, '%s');
            INSERT INTO t4 VALUES(3, '%s');
        ]], string10, string500, string500000))
    end, {
        -- <types-2.4.1>

        -- </types-2.4.1>
    })

test:do_execsql_test(
    "types-2.4.2",
    [[
        SELECT a FROM t4;
    ]], {
        -- <types-2.4.2>
        string10, string500, string500000
        -- </types-2.4.2>
    })

test:do_execsql_test(
    "types-2.5.1",
    [[
        DROP TABLE t1;
        DROP TABLE t2;
        DROP TABLE t3;
        DROP TABLE t4;
        CREATE TABLE t1(id primary key, a, b, c);
    ]], {
        -- <types-2.5.1>

        -- </types-2.5.1>
    })

test:do_test(
    "types-2.5.2",
    function()
        test:execsql("INSERT INTO t1 VALUES(1, NULL, '"..string10.."', 4000);")
        test:execsql("INSERT INTO t1 VALUES(2, '"..string500.."', 4000, NULL);")
        return test:execsql("INSERT INTO t1 VALUES(3, 4000, NULL, '"..string500000.."');")
    end, {
        -- <types-2.5.2>

        -- </types-2.5.2>
    })

test:do_execsql_test(
    "types-2.5.3",
    [[
        SELECT a,b,c FROM t1;
    ]], {
        -- <types-2.5.3>
        "", string10, 4000, string500, 4000, "", 4000, "", string500000
        -- </types-2.5.3>
    })



test:finish_test()
