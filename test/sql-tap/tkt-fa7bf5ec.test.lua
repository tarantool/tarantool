#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(1)

--!./tcltestrunner.lua
-- 2011 October 13
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library. Specifically,
-- it tests that ticket [fa7bf5ec94801e7e2030e41eefe5d9dd96eaacfd] has
-- been resolved.
--
-- The problem described by this ticket was that the sqlExprCompare()
-- function was saying that expressions (x='a') and (x='A') were identical
-- because it was using sqlStrICmp() instead of strcmp() to compare string
-- literals.  That was causing the query optimizer for aggregate queries to 
-- believe that both count() operations were identical, and thus only 
-- computing the first count() and making a copy of the result for the 
-- second count().
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "tkt-fa7bf5ec-1",
    [[
        CREATE TABLE t1(id INT primary key, x TEXT);
        INSERT INTO t1 VALUES (1, 'a');
        INSERT INTO t1 VALUES (2, 'A');
        INSERT INTO t1 VALUES (3, 'A');
        SELECT count(CASE WHEN x='a' THEN 1 END),
               count(CASE WHEN x='A' THEN 1 END)
          FROM t1;
    ]], {
        -- <tkt-fa7bf5ec-1>
        1, 2
        -- </tkt-fa7bf5ec-1>
    })

test:finish_test()

