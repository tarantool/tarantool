#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(9)

--!./tcltestrunner.lua
-- 2012 April 02
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- The tests in this file were used while developing the SQLite 4 code. 
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
testprefix = "tkt-385a5b56b9"
test:do_execsql_test(
    1.0,
    [[
        CREATE TABLE t1(id INT primary key, x INT, y INT);
        INSERT INTO t1 VALUES(1, 1, NULL);
        INSERT INTO t1 VALUES(2, 2, NULL);
        INSERT INTO t1 VALUES(3, 1, NULL);
    ]])

test:do_execsql_test(
    1.1,
    [[
        SELECT DISTINCT x, y FROM t1 
    ]], {
        -- <1.1>
        1, "", 2, ""
        -- </1.1>
    })

test:do_execsql_test(
    1.2,
    [[
        CREATE UNIQUE INDEX i1 ON t1(x, y)
    ]])

test:do_execsql_test(
    1.3,
    [[
        SELECT DISTINCT x, y FROM t1 
    ]], {
        -- <1.3>
        1, "", 2, ""
        -- </1.3>
    })

---------------------------------------------------------------------------
test:do_execsql_test(
    2.0,
    [[
        CREATE TABLE t2(x INT primary key, y INT NOT NULL);
        CREATE UNIQUE INDEX t2x ON t2(x);
        CREATE UNIQUE INDEX t2y ON t2(y);
    ]])

test:do_eqp_test(
    2.1,
    " SELECT DISTINCT x FROM t2 ", {
        -- <2.1>
        {0, 0, 0, "SCAN TABLE T2"}
        -- </2.1>
    })

test:do_eqp_test(
    2.2,
    " SELECT DISTINCT y FROM t2 ", {
        -- <2.2>
        -- changed after reordering indexes
        -- actually it does not matter, because each y val is
        -- distinct even in pk
        --{0, 0, 0, "SCAN TABLE t2 USING COVERING INDEX t2y"}
        {0, 0, 0, "SCAN TABLE T2"}
        -- </2.2>
    })

test:do_eqp_test(
    2.3,
    " SELECT DISTINCT x, y FROM t2 WHERE y=10 ", {
        -- <2.3>
        {0, 0, 0, "SEARCH TABLE T2 USING COVERING INDEX T2Y (Y=?)"}
        -- </2.3>
    })

test:do_eqp_test(
    2.4,
    " SELECT DISTINCT x, y FROM t2 WHERE x=10 ", {
        -- <2.4>
        -- changed after reordering indexes + add pk to x affected
        -- actually it does not matter
        --{0, 0, 0, "SEARCH TABLE t2 USING INDEX t2x (x=?)"}
        {0, 0, 0, "SEARCH TABLE T2 USING PRIMARY KEY (X=?)"}
        -- </2.4>
    })

test:finish_test()

