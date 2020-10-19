#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(11)

--!./tcltestrunner.lua
-- 2008 August 26
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- This file implements regression tests for sql library.
-- Specifically, it tests that bug #3334 has been fixed by the
-- addition of restriction (19) to the subquery flattener optimization.
--
-- $Id: tkt3334.test,v 1.1 2008/08/26 12:56:14 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "tkt3334-1.0",
    [[
        CREATE TABLE t1(id  INT primary key, a INT ,b INT );
        INSERT INTO t1 VALUES(1, 1,934);
        INSERT INTO t1 VALUES(2, 2,221);
        INSERT INTO t1 VALUES(3, 1,372);
        INSERT INTO t1 VALUES(4, 3,552);
        INSERT INTO t1 VALUES(5, 1,719);
        INSERT INTO t1 VALUES(6, 4,102);
        SELECT a,b FROM t1 ORDER BY b;
    ]], {
        -- <tkt3334-1.0>
        4, 102, 2, 221, 1, 372, 3, 552, 1, 719, 1, 934
        -- </tkt3334-1.0>
    })

test:do_execsql_test(
    "tkt3334-1.1",
    [[
        SELECT a FROM (SELECT a FROM t1 ORDER BY b LIMIT 2) WHERE a=1;
    ]], {
        -- <tkt3334-1.1>
        
        -- </tkt3334-1.1>
    })

test:do_execsql_test(
    "tkt3334-1.2",
    [[
        SELECT count(*) FROM (SELECT a FROM t1 ORDER BY b LIMIT 2) WHERE a=1;
    ]], {
        -- <tkt3334-1.2>
        0
        -- </tkt3334-1.2>
    })

test:do_execsql_test(
    "tkt3334-1.3",
    [[
        SELECT a FROM (SELECT a FROM t1 ORDER BY b LIMIT 3) WHERE a=1;
    ]], {
        -- <tkt3334-1.3>
        1
        -- </tkt3334-1.3>
    })

test:do_execsql_test(
    "tkt3334-1.4",
    [[
        SELECT count(*) FROM (SELECT a FROM t1 ORDER BY b LIMIT 3) WHERE a=1;
    ]], {
        -- <tkt3334-1.4>
        1
        -- </tkt3334-1.4>
    })

test:do_execsql_test(
    "tkt3334-1.5",
    [[
        SELECT a FROM (SELECT a FROM t1 ORDER BY b LIMIT 99) WHERE a=1;
    ]], {
        -- <tkt3334-1.5>
        1, 1, 1
        -- </tkt3334-1.5>
    })

test:do_execsql_test(
    "tkt3334-1.6",
    [[
        SELECT count(*) FROM (SELECT a FROM t1 ORDER BY b LIMIT 99) WHERE a=1;
    ]], {
        -- <tkt3334-1.6>
        3
        -- </tkt3334-1.6>
    })

test:do_execsql_test(
    "tkt3334-1.7",
    [[
        SELECT a FROM (SELECT a FROM t1 ORDER BY b) WHERE a=1;
    ]], {
        -- <tkt3334-1.7>
        1, 1, 1
        -- </tkt3334-1.7>
    })

test:do_execsql_test(
    "tkt3334-1.8",
    [[
        SELECT count(*) FROM (SELECT a FROM t1 ORDER BY b) WHERE a=1;
    ]], {
        -- <tkt3334-1.8>
        3
        -- </tkt3334-1.8>
    })

test:do_execsql_test(
    "tkt3334-1.9",
    [[
        SELECT a FROM (SELECT a FROM t1) WHERE a=1;
    ]], {
        -- <tkt3334-1.9>
        1, 1, 1
        -- </tkt3334-1.9>
    })

test:do_execsql_test(
    "tkt3334-1.10",
    [[
        SELECT count(*) FROM (SELECT a FROM t1) WHERE a=1;
    ]], {
        -- <tkt3334-1.10>
        3
        -- </tkt3334-1.10>
    })

test:finish_test()