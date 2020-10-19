#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(3)

--!./tcltestrunner.lua
-- 2011 July 8
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library.  The
-- focus of this file is testing that bug [54844eea3f] has been fixed.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
local testprefix = "tkt-54844eea3f"
test:do_execsql_test(
    "1.0",
    [[
        CREATE TABLE t1(a INTEGER PRIMARY KEY);
        INSERT INTO t1 VALUES(1);
        INSERT INTO t1 VALUES(4);

        CREATE TABLE t2(b INTEGER PRIMARY KEY);
        INSERT INTO t2 VALUES(1);
        INSERT INTO t2 VALUES(2);
        INSERT INTO t2 SELECT b+2 FROM t2;
        INSERT INTO t2 SELECT b+4 FROM t2;
        INSERT INTO t2 SELECT b+8 FROM t2;
        INSERT INTO t2 SELECT b+16 FROM t2;

        CREATE TABLE t3(c INTEGER PRIMARY KEY);
        INSERT INTO t3 VALUES(1);
        INSERT INTO t3 VALUES(2);
        INSERT INTO t3 VALUES(3);
    ]], {
        -- <1.0>
        
        -- </1.0>
    })

test:do_execsql_test(
    "1.1",
    [[
        SELECT 'test-2', t3.c, (
              SELECT count(*) 
              FROM t1 JOIN (SELECT DISTINCT t3.c AS p FROM t2) AS x ON t1.a=x.p
        )
        FROM t3;
    ]], {
        -- <1.1>
        "test-2", 1, 1, "test-2", 2, 0, "test-2", 3, 0
        -- </1.1>
    })

test:do_execsql_test(
    "1.2",
    [[
        CREATE TABLE t4(id INT primary key, a TEXT, b TEXT, c TEXT);
        INSERT INTO t4 VALUES(1, 'a', '1', 'one');
        INSERT INTO t4 VALUES(2, 'a', '2', 'two');
        INSERT INTO t4 VALUES(3, 'b', '1', 'three');
        INSERT INTO t4 VALUES(4, 'b', '2', 'four');
        SELECT ( 
          SELECT c FROM (
            SELECT a,b,c FROM t4 WHERE a=output.a ORDER BY b LIMIT 10 OFFSET 1
          ) WHERE b=output.b
        ) FROM t4 AS output;
    ]], {
        -- <1.2>
        "", "two", "", "four"
        -- </1.2>
    })

test:finish_test()

