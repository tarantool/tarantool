#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(8)

--!./tcltestrunner.lua
-- 2012 December 19
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
-- it tests that ticket [a7b7803e8d1e8699cd8a460a38133b98892d2e17] has
-- been fixed.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- ["source",[["testdir"],"\/lock_common.tcl"]]
-- ["source",[["testdir"],"\/malloc_common.tcl"]]
test:do_test(
    "tkt-a7b7803e.1",
    function()
        return test:execsql [[
            CREATE TABLE t1(a INT primary key,b TEXT);
            INSERT INTO t1 VALUES(0,'first'),(99,'fuzzy');
            SELECT (t1.a==0) AS x, b
              FROM t1
             WHERE a=0 OR x;
        ]]
    end, {
        -- <tkt-a7b7803e.1>
        true, "first"
        -- </tkt-a7b7803e.1>
    })

test:do_test(
    "tkt-a7b7803e.2",
    function()
        return test:execsql [[
            SELECT a, (t1.b='fuzzy') AS x
              FROM t1
             WHERE x
        ]]
    end, {
        -- <tkt-a7b7803e.2>
        99, true
        -- </tkt-a7b7803e.2>
    })

test:do_test(
    "tkt-a7b7803e.3",
    function()
        return test:execsql [[
            SELECT (a=99) AS x, (t1.b='fuzzy') AS y, *
              FROM t1
             WHERE x AND y
        ]]
    end, {
        -- <tkt-a7b7803e.3>
        true, true, 99, "fuzzy"
        -- </tkt-a7b7803e.3>
    })

test:do_test(
    "tkt-a7b7803e.4",
    function()
        return test:execsql [[
            SELECT (a=99) AS x, (t1.b='first') AS y, *
              FROM t1
             WHERE x OR y
             ORDER BY a
        ]]
    end, {
        -- <tkt-a7b7803e.4>
        false, true, 0, "first", true, false, 99, "fuzzy"
        -- </tkt-a7b7803e.4>
    })

test:do_test(
    "tkt-a7b7803e.5",
    function()
        return test:execsql [[
            SELECT (M.a=99) AS x, M.b, (N.b='first') AS y, N.b
              FROM t1 M, t1 N
             WHERE x OR y
             ORDER BY M.a, N.a
        ]]
    end, {
        -- <tkt-a7b7803e.5>
        false, "first", true, "first", true, "fuzzy", true, "first", true, "fuzzy", false, "fuzzy"
        -- </tkt-a7b7803e.5>
    })

test:do_test(
    "tkt-a7b7803e.6",
    function()
        return test:execsql [[
            SELECT (M.a=99) AS x, M.b, (N.b='first') AS y, N.b
              FROM t1 M, t1 N
             WHERE x AND y
             ORDER BY M.a, N.a
        ]]
    end, {
        -- <tkt-a7b7803e.6>
        true, "fuzzy", true, "first"
        -- </tkt-a7b7803e.6>
    })

test:do_test(
    "tkt-a7b7803e.7",
    function()
        return test:execsql [[
            SELECT (M.a=99) AS x, M.b, (N.b='first') AS y, N.b
              FROM t1 M JOIN t1 N ON x AND y
             ORDER BY M.a, N.a
        ]]
    end, {
        -- <tkt-a7b7803e.7>
        true, "fuzzy", true, "first"
        -- </tkt-a7b7803e.7>
    })

test:do_test(
    "tkt-a7b7803e.8",
    function()
        return test:execsql [[
            SELECT (M.a=99) AS x, M.b, (N.b='first') AS y, N.b
              FROM t1 M JOIN t1 N ON x
             ORDER BY M.a, N.a
        ]]
    end, {
        -- <tkt-a7b7803e.8>
        true, "fuzzy", true, "first", true, "fuzzy", false, "fuzzy"
        -- </tkt-a7b7803e.8>
    })

test:finish_test()

