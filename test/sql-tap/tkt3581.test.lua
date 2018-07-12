#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(3)

--!./tcltestrunner.lua
-- 2008 January 13
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
-- This file implements tests to verify that ticket #3581 has been
-- fixed.  
--
-- $Id: tkt3581.test,v 1.1 2009/01/14 01:10:40 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_test(
    "tkt3581-1.1",
    function()
        return test:execsql [[
            CREATE TABLE t1(a INTEGER PRIMARY KEY, b INT, c INT);
            INSERT INTO t1 VALUES(0,544,846);
            INSERT INTO t1 VALUES(1,345,51);
            CREATE TABLE t2(a INTEGER PRIMARY KEY, b INT, c INT);
            INSERT INTO t2 SELECT * FROM t1;
            CREATE INDEX i2 on t2(c);
        ]]
    end, {
        -- <tkt3581-1.1>
        
        -- </tkt3581-1.1>
    })

test:do_test(
    "tkt3581-1.2",
    function()
        return test:execsql [[
            SELECT a FROM t1
             WHERE (b > 45 AND c < 356)
                OR b <= 733
                OR b >= 557
                OR (b >= 614 AND c < 251)
             ORDER BY b;
        ]]
    end, {
        -- <tkt3581-1.2>
        1, 0
        -- </tkt3581-1.2>
    })

test:do_test(
    "tkt3581-1.3",
    function()
        return test:execsql [[
            SELECT a FROM t2
             WHERE (b > 45 AND c < 356)
                OR b <= 733
                OR b >= 557
                OR (b >= 614 AND c < 251)
             ORDER BY b;
        ]]
    end, {
        -- <tkt3581-1.3>
        1, 0
        -- </tkt3581-1.3>
    })

test:finish_test()

