#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(3)

--!./tcltestrunner.lua
-- 2009 May 25
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
-- Tests to verify ticket #3879 is fixed.
--
-- $Id: tkt3879.test,v 1.2 2009/06/05 17:09:12 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "tkt3879.1.1",
    [[
        CREATE TABLE t1 (a TEXT PRIMARY KEY, b INT );
        INSERT INTO t1 VALUES ('w',  1);
        INSERT INTO t1 VALUES ('z', -1);

        CREATE TABLE t2 (m INTEGER PRIMARY KEY, n INT , a TEXT, p INT );
        INSERT INTO t2 VALUES (25, 13, 'w', 1);
        INSERT INTO t2 VALUES (26, 25, 'z', 153);
        INSERT INTO t2 VALUES (27, 25, 'z', 68);

        CREATE TABLE t3 (m  INT PRIMARY KEY);
        INSERT INTO t3 VALUES (25);
    ]], {
        -- <tkt3879.1.1>
        
        -- </tkt3879.1.1>
    })

test:do_execsql_test(
    "tkt3879.1.2",
    [[
        SELECT 111, t1.b*123
        FROM t3, t2 AS j0, t2 AS j1, t1
        WHERE j0.m=t3.m AND t1.a=j0.a AND j1.n=j0.m;
    ]], {
        -- <tkt3879.1.2>
        111, 123, 111, 123
        -- </tkt3879.1.2>
    })

test:do_execsql_test(
    "tkt3879.1.3",
    [[
        SELECT 222, t1.b*123
        FROM t3, t2 AS j0, t2 AS j1, t1
        WHERE j0.m=t3.m AND t1.a=j0.a AND j1.n=j0.m
        ORDER BY t1.b;
    ]], {
        -- <tkt3879.1.3>
        222, 123, 222, 123
        -- </tkt3879.1.3>
    })

test:finish_test()

