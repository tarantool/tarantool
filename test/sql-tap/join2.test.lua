#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(7)

--!./tcltestrunner.lua
-- 2002 May 24
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
-- This file implements tests for joins, including outer joins.
--
-- $Id: join2.test,v 1.2 2005/01/21 03:12:16 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "join2-1.1",
    [[
        CREATE TABLE t1(a INT primary key,b INT);
        INSERT INTO t1 VALUES(1,11);
        INSERT INTO t1 VALUES(2,22);
        INSERT INTO t1 VALUES(3,33);
        SELECT * FROM t1;
    ]], {
        -- <join2-1.1>
        1, 11, 2, 22, 3, 33
        -- </join2-1.1>
    })

test:do_execsql_test(
    "join2-1.2",
    [[
        CREATE TABLE t2(b INT primary key,c INT);
        INSERT INTO t2 VALUES(11,111);
        INSERT INTO t2 VALUES(33,333);
        INSERT INTO t2 VALUES(44,444);
        SELECT * FROM t2;
    ]], {
        -- <join2-1.2>
        11, 111, 33, 333, 44, 444
        -- </join2-1.2>
    })

test:do_execsql_test(
    "join2-1.3",
    [[
        CREATE TABLE t3(c INT primary key,d INT);
        INSERT INTO t3 VALUES(111,1111);
        INSERT INTO t3 VALUES(444,4444);
        INSERT INTO t3 VALUES(555,5555);
        SELECT * FROM t3;
    ]], {
        -- <join2-1.3>
        111, 1111, 444, 4444, 555, 5555
        -- </join2-1.3>
    })

test:do_execsql_test(
    "join2-1.4",
    [[
        SELECT * FROM
          t1 NATURAL JOIN t2 NATURAL JOIN t3
    ]], {
        -- <join2-1.4>
        1, 11, 111, 1111
        -- </join2-1.4>
    })

test:do_execsql_test(
    "join2-1.5",
    [[
        SELECT * FROM
          t1 NATURAL JOIN t2 NATURAL LEFT OUTER JOIN t3
    ]], {
        -- <join2-1.5>
        1, 11, 111, 1111, 3, 33, 333, ""
        -- </join2-1.5>
    })

test:do_execsql_test(
    "join2-1.6",
    [[
        SELECT * FROM
          t1 NATURAL LEFT OUTER JOIN t2 NATURAL JOIN t3
    ]], {
        -- <join2-1.6>
        1, 11, 111, 1111
        -- </join2-1.6>
    })

test:do_execsql_test(
    "join2-1.7",
    [[
        SELECT * FROM
          t1 NATURAL LEFT OUTER JOIN (t2 NATURAL JOIN t3)
    ]], {
        -- <join2-1.7>
        1, 11, 111, 1111, 2, 22, "", "", 3, 33, "", ""
        -- </join2-1.7>
    })



test:finish_test()

