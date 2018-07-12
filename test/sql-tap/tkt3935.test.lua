#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(10)

--!./tcltestrunner.lua
-- 2009 July 1
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library.
--
-- This file implements tests to verify that ticket #3935 has been fixed.
--
-- $Id: tkt3935.test,v 1.2 2009/07/01 16:12:08 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "tkt3935.1",
    [[
        CREATE TABLE t1(a INT primary key, b INT);
        CREATE TABLE t2(c INT primary key, d INT);
    ]], {
        -- <tkt3935.1>
        
        -- </tkt3935.1>
    })

test:do_execsql_test(
    "tkt3935.2",
    [[
        SELECT j1.b FROM ( SELECT * FROM t1 INNER JOIN t2 ON a=c ) AS j1 
    ]], {
        -- <tkt3935.2>
        
        -- </tkt3935.2>
    })

test:do_execsql_test(
    "tkt3935.3",
    [[
        SELECT j1.b FROM (t1 INNER JOIN t2 ON a=c) AS j1 
    ]], {
        -- <tkt3935.3>
        
        -- </tkt3935.3>
    })

test:do_catchsql_test(
    "tkt3935.4",
    [[
        SELECT a FROM (t1) AS t ON b USING(a) 
    ]], {
        -- <tkt3935.4>
        1, "a JOIN clause is required before ON"
        -- </tkt3935.4>
    })

test:do_catchsql_test(
    "tkt3935.5",
    [[
        SELECT a FROM (t1) AS t ON b 
    ]], {
        -- <tkt3935.5>
        1, "a JOIN clause is required before ON"
        -- </tkt3935.5>
    })

test:do_catchsql_test(
    "tkt3935.6",
    [[
        SELECT a FROM (SELECT * FROM t1) AS t ON b USING(a) 
    ]], {
        -- <tkt3935.6>
        1, "a JOIN clause is required before ON"
        -- </tkt3935.6>
    })

test:do_catchsql_test(
    "tkt3935.7",
    [[
        SELECT a FROM (SELECT * FROM t1) AS t ON b 
    ]], {
        -- <tkt3935.7>
        1, "a JOIN clause is required before ON"
        -- </tkt3935.7>
    })

test:do_catchsql_test(
    "tkt3935.8",
    [[
        SELECT a FROM t1 AS t ON b 
    ]], {
        -- <tkt3935.8>
        1, "a JOIN clause is required before ON"
        -- </tkt3935.8>
    })

test:do_catchsql_test(
    "tkt3935.9",
    [[
        SELECT a FROM t1 AS t ON b USING(a) 
    ]], {
        -- <tkt3935.9>
        1, "a JOIN clause is required before ON"
        -- </tkt3935.9>
    })

test:do_catchsql_test(
    "tkt3935.10",
    [[
        SELECT a FROM t1 AS t USING(a) 
    ]], {
        -- <tkt3935.10>
        1, "a JOIN clause is required before USING"
        -- </tkt3935.10>
    })

test:finish_test()

