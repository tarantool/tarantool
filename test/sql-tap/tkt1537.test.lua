#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(15)

--!./tcltestrunner.lua
-- 2005 November 26
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
-- This file implements tests to verify that ticket #1537 is
-- fixed.  
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "tkt1537-1.1",
    [[
        CREATE TABLE t1(id INT primary key, a1 INT, a2 INT);
        INSERT INTO t1 VALUES(1, NULL, NULL);
        INSERT INTO t1 VALUES(2, 1, 3);
        CREATE TABLE t2(id INT primary key, b INT);
        INSERT INTO t2 VALUES(3, 1);
        INSERT INTO t2 VALUES(4, NULL);
        SELECT * FROM t1 LEFT JOIN t2 ON a1=b OR a2=+b;
    ]], {
        -- <tkt1537-1.1>
        1, "", "", "", "", 2, 1, 3, 3, 1
        -- </tkt1537-1.1>
    })

test:do_execsql_test(
    "tkt1537-1.2",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON a1=b OR a2=b;
    ]], {
        -- <tkt1537-1.2>
        1, "", "", "", "", 2, 1, 3, 3, 1
        -- </tkt1537-1.2>
    })

test:do_execsql_test(
    "tkt1537-1.3",
    [[
        SELECT * FROM t2 LEFT JOIN t1 ON a1=b OR a2=b;
    ]], {
        -- <tkt1537-1.3>
        3, 1, 2, 1, 3, 4, "", "", "", ""
        -- </tkt1537-1.3>
    })

test:do_execsql_test(
    "tkt1537-1.4",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON b IN (a1,a2);
    ]], {
        -- <tkt1537-1.4>
        1, "", "", "", "", 2, 1, 3, 3, 1
        -- </tkt1537-1.4>
    })

test:do_execsql_test(
    "tkt1537-1.5",
    [[
        SELECT * FROM t2 LEFT JOIN t1 ON b IN (a2,a1);
    ]], {
        -- <tkt1537-1.5>
        3, 1, 2, 1, 3, 4, "", "", "", ""
        -- </tkt1537-1.5>
    })



test:do_execsql_test(
    "tkt1537-1.6",
    [[
        CREATE INDEX t1a1 ON t1(a1);
        CREATE INDEX t1a2 ON t1(a2);
        CREATE INDEX t2b ON t2(b);
        SELECT * FROM t1 LEFT JOIN t2 ON a1=b OR a2=b;
    ]], {
        -- <tkt1537-1.6>
        1, "", "", "", "", 2, 1, 3, 3, 1
        -- </tkt1537-1.6>
    })

test:do_execsql_test(
    "tkt1537-1.7",
    [[
        SELECT * FROM t2 LEFT JOIN t1 ON a1=b OR a2=b;
    ]], {
        -- <tkt1537-1.7>
        -- order changed after reordering indexes (it is ok because order is not specified)
        --4, "", "", "", "", 3, 1, 2, 1, 3
        3,1,2,1,3,4,"","","",""
        -- </tkt1537-1.7>
    })

test:do_execsql_test(
    "tkt1537-1.8",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON b IN (a1,a2);
    ]], {
        -- <tkt1537-1.8>
        1, "", "", "", "", 2, 1, 3, 3, 1
        -- </tkt1537-1.8>
    })

test:do_execsql_test(
    "tkt1537-1.9",
    [[
        SELECT * FROM t2 LEFT JOIN t1 ON b IN (a2,a1);
    ]], {
        -- <tkt1537-1.9>
        -- order changed after reordering indexes (it is ok because order is not specified)
        --4, "", "", "", "", 3, 1, 2, 1, 3
        3,1,2,1,3,4,"","","",""
        -- </tkt1537-1.9>
    })



test:execsql [[
    DROP INDEX t1a1 ON t1;
    DROP INDEX t1a2 ON t1;
    DROP INDEX t2b ON t2;
]]
test:do_execsql_test(
    "tkt1537-2.1",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON b BETWEEN a1 AND a2;
    ]], {
        -- <tkt1537-2.1>
        1, "", "", "", "", 2, 1, 3, 3, 1
        -- </tkt1537-2.1>
    })

test:do_execsql_test(
    "tkt1537-2.2",
    [[
        CREATE INDEX t2b ON t2(b);
        SELECT * FROM t1 LEFT JOIN t2 ON b BETWEEN a1 AND a2;
    ]], {
        -- <tkt1537-2.2>
        1, "", "", "", "", 2, 1, 3, 3, 1
        -- </tkt1537-2.2>
    })

test:do_execsql_test(
    "tkt1537-2.3",
    [[
        SELECT * FROM t2 LEFT JOIN t1 ON b BETWEEN a1 AND a2;
    ]], {
        -- <tkt1537-2.3>
        -- order changed after reordering indexes (it is ok because order is not specified)
        --4, "", "", "", "", 3, 1, 2, 1, 3
        3,1,2,1,3,4,"","","",""


;
    -- </tkt1537-2.3>
    })

test:do_execsql_test(
    "tkt1537-2.4",
    [[
        CREATE INDEX t1a1 ON t1(a1);
        CREATE INDEX t1a2 ON t1(a2);
        SELECT * FROM t2 LEFT JOIN t1 ON b BETWEEN a1 AND a2;
    ]], {
        -- <tkt1537-2.4>
        -- order changed after reordering indexes (it is ok because order is not specified)
        --4, "", "", "", "", 3, 1, 2, 1, 3
        3,1,2,1,3,4,"","","",""
        -- </tkt1537-2.4>
    })

test:do_execsql_test(
    "tkt1537-3.1",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON b GLOB 'abc*' WHERE t1.id=1;
    ]], {
        -- <tkt1537-3.1>
        1, "", "", "", ""
        -- </tkt1537-3.1>
    })

test:do_execsql_test(
    "tkt1537-3.2",
    [[
        SELECT * FROM t2 LEFT JOIN t1 ON a1 GLOB 'abc*' WHERE t2.id=3;
    ]], {
        -- <tkt1537-3.2>
        3, 1, "", "", ""
        -- </tkt1537-3.2>
    })

test:finish_test()

