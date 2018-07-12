#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(6)

--!./tcltestrunner.lua
-- 2007 Dec 12
--
-- The author disclaims copyright to this source code. In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- This file is to test that ticket #2832 has been fixed.
--
-- $Id: tkt2832.test,v 1.5 2009/04/07 14:14:23 danielk1977 Exp $
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


test:do_execsql_test(
    "tkt2832-1.1",
    [[
        CREATE TABLE t1(a INT PRIMARY KEY);
        INSERT INTO t1 VALUES(2);
        INSERT INTO t1 VALUES(1);
        INSERT INTO t1 VALUES(3);
    ]], {
        -- <tkt2832-1.1>
        
        -- </tkt2832-1.1>
    })


test:do_execsql_test(
    "tkt2832-1.2",
    [[
        UPDATE OR REPLACE t1 SET a = 1;
        SELECT * FROM t1;
    ]], {
        -- <tkt2832-1.2>
        1
        -- </tkt2832-1.2>
    })

test:do_execsql_test(
    "tkt2832-2.1",
    [[
        CREATE TABLE t2(a INT primary key, b INT);
        CREATE TRIGGER t2_t AFTER UPDATE ON t2 BEGIN
          DELETE FROM t2 WHERE a = new.a + 1;
        END;
        INSERT INTO t2 VALUES(1, 2);
        INSERT INTO t2 VALUES(2, 3);
    ]], {
        -- <tkt2832-2.1>

        -- </tkt2832-2.1>
    })

test:do_execsql_test(
    "tkt2832-2.2",
    [[
        UPDATE t2 SET b = 5
    ]], {
        -- <tkt2832-2.2>

        -- </tkt2832-2.2>
    })

test:do_execsql_test(
    "tkt2832-3.1",
    [[
        CREATE TABLE t3(a INT primary key, b INT);
        CREATE TRIGGER t3_t AFTER DELETE ON t3 BEGIN
          DELETE FROM t3 WHERE a = old.a + 1;
        END;
        INSERT INTO t3 VALUES(1, 2);
        INSERT INTO t3 VALUES(2, 3);
    ]], {
        -- <tkt2832-3.1>

        -- </tkt2832-3.1>
    })

test:do_execsql_test(
    "tkt2832-3.2",
    [[
        DELETE FROM t3 WHERE 1
    ]], {
        -- <tkt2832-3.2>

        -- </tkt2832-3.2>
    })

test:finish_test()

