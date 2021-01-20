#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(22)

--!./tcltestrunner.lua
-- 2015-10-06
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
-- This file implements test cases for the [b65cb2c8d91f6685841d7d1e13b6]
-- bug:  Correct handling of LIMIT and OFFSET on a UNION ALL query where
-- the right-hand SELECT contains an ORDER BY in a subquery.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


test:do_execsql_test(
    "offset1-1.1",
    [[
        CREATE TABLE t1(a INT primary key,b TEXT);
        INSERT INTO t1 VALUES(1,'a'),(2,'b'),(3,'c'),(4,'d'),(5,'e');
        CREATE TABLE t2(id INT primary key, x INT,y TEXT);
        INSERT INTO t2 VALUES(1, 8,'y'),(2, 9,'z'),(3, 6,'w'),(4, 7,'x');
        SELECT count(*) FROM t1, t2;
    ]], {
        -- <offset1-1.1>
        20
        -- </offset1-1.1>
    })

test:do_execsql_test(
    "offset1-1.2.0",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 3 OFFSET 0;
    ]], {
        -- <offset1-1.2.0>
        1, "a", 2, "b", 3, "c"
        -- </offset1-1.2.0>
    })

test:do_execsql_test(
    "offset1-1.2.1",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 3 OFFSET 1;
    ]], {
        -- <offset1-1.2.1>
        2, "b", 3, "c", 4, "d"
        -- </offset1-1.2.1>
    })

test:do_execsql_test(
    "offset1-1.2.2",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 3 OFFSET 2;
    ]], {
        -- <offset1-1.2.2>
        3, "c", 4, "d", 5, "e"
        -- </offset1-1.2.2>
    })

test:do_execsql_test(
    "offset1-1.2.3",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 3 OFFSET 3;
    ]], {
        -- <offset1-1.2.3>
        4, "d", 5, "e", 6, "w"
        -- </offset1-1.2.3>
    })

test:do_execsql_test(
    "offset1-1.2.4",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 3 OFFSET 4;
    ]], {
        -- <offset1-1.2.4>
        5, "e", 6, "w", 7, "x"
        -- </offset1-1.2.4>
    })

test:do_execsql_test(
    "offset1-1.2.5",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 3 OFFSET 5;
    ]], {
        -- <offset1-1.2.5>
        6, "w", 7, "x", 8, "y"
        -- </offset1-1.2.5>
    })

test:do_execsql_test(
    "offset1-1.2.6",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 3 OFFSET 6;
    ]], {
        -- <offset1-1.2.6>
        7, "x", 8, "y", 9, "z"
        -- </offset1-1.2.6>
    })

test:do_execsql_test(
    "offset1-1.2.7",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 3 OFFSET 7;
    ]], {
        -- <offset1-1.2.7>
        8, "y", 9, "z"
        -- </offset1-1.2.7>
    })

test:do_execsql_test(
    "offset1-1.2.8",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 3 OFFSET 8;
    ]], {
        -- <offset1-1.2.8>
        9, "z"
        -- </offset1-1.2.8>
    })

test:do_execsql_test(
    "offset1-1.2.9",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 3 OFFSET 9;
    ]], {
        -- <offset1-1.2.9>

        -- </offset1-1.2.9>
    })

test:do_execsql_test(
    "offset1-1.3.0",
    [[
        SELECT * FROM t1 LIMIT 0;
    ]], {
        -- <offset1-1.3.0>

        -- </offset1-1.3.0>
    })

test:do_execsql_test(
    "offset1-1.4.0",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 0 OFFSET 1;
    ]], {
        -- <offset1-1.4.0>

        -- </offset1-1.4.0>
    })

test:do_execsql_test(
    "offset1-1.4.1",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 1 OFFSET 1;
    ]], {
        -- <offset1-1.4.1>
        2, "b"
        -- </offset1-1.4.1>
    })

test:do_execsql_test(
    "offset1-1.4.2",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 2 OFFSET 1;
    ]], {
        -- <offset1-1.4.2>
        2, "b", 3, "c"
        -- </offset1-1.4.2>
    })

test:do_execsql_test(
    "offset1-1.4.3",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 3 OFFSET 1;
    ]], {
        -- <offset1-1.4.3>
        2, "b", 3, "c", 4, "d"
        -- </offset1-1.4.3>
    })

test:do_execsql_test(
    "offset1-1.4.4",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 4 OFFSET 1;
    ]], {
        -- <offset1-1.4.4>
        2, "b", 3, "c", 4, "d", 5, "e"
        -- </offset1-1.4.4>
    })

test:do_execsql_test(
    "offset1-1.4.5",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 5 OFFSET 1;
    ]], {
        -- <offset1-1.4.5>
        2, "b", 3, "c", 4, "d", 5, "e", 6, "w"
        -- </offset1-1.4.5>
    })

test:do_execsql_test(
    "offset1-1.4.6",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 6 OFFSET 1;
    ]], {
        -- <offset1-1.4.6>
        2, "b", 3, "c", 4, "d", 5, "e", 6, "w", 7, "x"
        -- </offset1-1.4.6>
    })

test:do_execsql_test(
    "offset1-1.4.7",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 7 OFFSET 1;
    ]], {
        -- <offset1-1.4.7>
        2, "b", 3, "c", 4, "d", 5, "e", 6, "w", 7, "x", 8, "y"
        -- </offset1-1.4.7>
    })

test:do_execsql_test(
    "offset1-1.4.8",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 8 OFFSET 1;
    ]], {
        -- <offset1-1.4.8>
        2, "b", 3, "c", 4, "d", 5, "e", 6, "w", 7, "x", 8, "y", 9, "z"
        -- </offset1-1.4.8>
    })

test:do_execsql_test(
    "offset1-1.4.9",
    [[
        SELECT a, b FROM t1
        UNION ALL
        SELECT * FROM (SELECT x, y FROM t2 ORDER BY y)
        LIMIT 9 OFFSET 1;
    ]], {
        -- <offset1-1.4.9>
        2, "b", 3, "c", 4, "d", 5, "e", 6, "w", 7, "x", 8, "y", 9, "z"
        -- </offset1-1.4.9>
    })



test:finish_test()
