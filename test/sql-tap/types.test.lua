#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(14)

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
-- This file implements regression tests for sql library. Specfically
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
        CREATE TABLE t1(id  INT primary key, a integer);
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
--     set root [db eval {select rootpage from sql_master where name = 't1'}]
--     record_sizes $root
--   } {3 3 3 4 4 6 6 10 10}
-- } else {
--   do_test types-2.1.9 {
--     set root [db eval {select rootpage from sql_master where name = 't1'}]
--     record_sizes $root
--   } {2 3 3 4 4 6 6 10 10}
-- }
-- Insert some reals. These should be 10 byte records.
test:do_execsql_test(
    "types-2.2.1",
    [[
        CREATE TABLE t2(id  INT primary key, a NUMBER);
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
--     set root [db eval {select rootpage from sql_master where name = 't2'}]
--     record_sizes $root
--   } {3 10 10}
-- } else {
--   do_test types-2.2.3 {
--     set root [db eval {select rootpage from sql_master where name = 't2'}]
--     record_sizes $root
--   } {2 10 10}
-- }
-- Insert a NULL. This should be a two byte record.
test:do_execsql_test(
    "types-2.3.1",
    [[
        CREATE TABLE t3(id  INT primary key, a INT null);
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
        true
        -- </types-2.3.2>
    })

-- # Check that all the record sizes are as we expected.
-- do_test types-2.3.3 {
--   set root [db eval {select rootpage from sql_master where name = 't3'}]
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
            CREATE TABLE t4(id  INT primary key, a TEXT);
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

test:finish_test()
