#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(197)

--!./tcltestrunner.lua
-- 2008 June 24
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
-- The focus of this file is testing the compound-SELECT merge
-- optimization.  Or, in other words, making sure that all
-- possible combinations of UNION, UNION ALL, EXCEPT, and
-- INTERSECT work together with an ORDER BY clause (with or w/o
-- explicit sort order and explicit collating secquites) and
-- with and without optional LIMIT and OFFSET clauses.
--
-- $Id: selectA.test,v 1.6 2008/08/21 14:24:29 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
testprefix = "selectA"


test:do_execsql_test(
    "selectA-1.0",
    [[
        CREATE TABLE t1(id primary key, a,b,c COLLATE NOCASE);
        INSERT INTO t1 VALUES(1, 1,'a','a');
        INSERT INTO t1 VALUES(2, 9.9, 'b', 'B');
        INSERT INTO t1 VALUES(3, NULL, 'C', 'c');
        INSERT INTO t1 VALUES(4, 'hello', 'd', 'D');
        INSERT INTO t1 VALUES(5, x'616263', 'e', 'e');
        SELECT a,b,c FROM t1;
    ]], {
        -- <selectA-1.0>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e"
        -- </selectA-1.0>
    })

test:do_execsql_test(
    "selectA-1.1",
    [[
        CREATE TABLE t2(id primary key, x,y,z COLLATE NOCASE);
        INSERT INTO t2 VALUES(1, NULL,'U','u');
        INSERT INTO t2 VALUES(2, 'mad', 'Z', 'z');
        INSERT INTO t2 VALUES(3, x'68617265', 'm', 'M');
        INSERT INTO t2 VALUES(4, 5.2e6, 'X', 'x');
        INSERT INTO t2 VALUES(5, -23, 'Y', 'y');
        SELECT x,y,z FROM t2;
    ]], {
        -- <selectA-1.1>
        "", "U", "u", "mad", "Z", "z", "hare", "m", "M", 5200000.0, "X", "x", -23, "Y", "y"
        -- </selectA-1.1>
    })

test:do_execsql_test(
    "selectA-1.2",
    [[
        CREATE TABLE t3(id primary key, a,b,c COLLATE NOCASE);
        INSERT INTO t3 SELECT id, a, b, c FROM t1;
        INSERT INTO t3 SELECT id+10, x, y, z FROM t2;
        INSERT INTO t3 SELECT id+20, a, b, c FROM t1;
        INSERT INTO t3 SELECT id+30, x, y, z FROM t2;
        INSERT INTO t3 SELECT id+40, a, b, c FROM t1;
        INSERT INTO t3 SELECT id+50, x, y, z FROM t2;
        SELECT count(*) FROM t3;
    ]], {
        -- <selectA-1.2>
        30
        -- </selectA-1.2>
    })

-- MUST_WORK_TEST
if (0 > 0)
 then
end
test:do_execsql_test(
    "selectA-2.1",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY a,b,c
    ]], {
        -- <selectA-2.1>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.1>
    })

test:do_test(
    "selectA-2.1.1",
    function()
        -- Ticket #3314
        return test:execsql [[
            SELECT t1.a, t1.b, t1.c FROM t1 UNION ALL SELECT x,y,z FROM t2
            ORDER BY a,b,c
        ]]
    end, {
        -- <selectA-2.1.1>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.1.1>
    })

test:do_test(
    "selectA-2.1.2",
    function()
        -- Ticket #3314
        return test:execsql [[
            SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
            ORDER BY t1.a, t1.b, t1.c
        ]]
    end, {
        -- <selectA-2.1.2>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.1.2>
    })

test:do_execsql_test(
    "selectA-2.2",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY a DESC,b,c
    ]], {
        -- <selectA-2.2>
        "hare", "m", "M", "abc", "e", "e", "mad", "Z", "z", "hello", "d", "D", 5200000.0, "X", "x", 9.9, "b", "B", 1, "a", "a", -23, "Y", "y", "", "C", "c", "", "U", "u"
        -- </selectA-2.2>
    })

test:do_execsql_test(
    "selectA-2.3",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY a,c,b
    ]], {
        -- <selectA-2.3>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.3>
    })

test:do_execsql_test(
    "selectA-2.4",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY b,a,c
    ]], {
        -- <selectA-2.4>
        "", "C", "c", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z", 1, "a", "a", 9.9, "b", "B", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.4>
    })

test:do_execsql_test(
    "selectA-2.5",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY b COLLATE NOCASE,a,c
    ]], {
        -- <selectA-2.5>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.5>
    })

test:do_execsql_test(
    "selectA-2.6",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY b COLLATE NOCASE DESC,a,c
    ]], {
        -- <selectA-2.6>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-2.6>
    })

test:do_execsql_test(
    "selectA-2.7",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY c,b,a
    ]], {
        -- <selectA-2.7>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.7>
    })

test:do_execsql_test(
    "selectA-2.8",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY c,a,b
    ]], {
        -- <selectA-2.8>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.8>
    })

test:do_execsql_test(
    "selectA-2.9",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY c DESC,a,b
    ]], {
        -- <selectA-2.9>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-2.9>
    })

test:do_execsql_test(
    "selectA-2.10",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY c COLLATE BINARY DESC,a,b
    ]], {
        -- <selectA-2.10>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "abc", "e", "e", "", "C", "c", 1, "a", "a", "hare", "m", "M", "hello", "d", "D", 9.9, "b", "B"
        -- </selectA-2.10>
    })

test:do_execsql_test(
    "selectA-2.11",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY a,b,c
    ]], {
        -- <selectA-2.11>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.11>
    })

test:do_execsql_test(
    "selectA-2.12",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY a DESC,b,c
    ]], {
        -- <selectA-2.12>
        "hare", "m", "M", "abc", "e", "e", "mad", "Z", "z", "hello", "d", "D", 5200000.0, "X", "x", 9.9, "b", "B", 1, "a", "a", -23, "Y", "y", "", "C", "c", "", "U", "u"
        -- </selectA-2.12>
    })

test:do_execsql_test(
    "selectA-2.13",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY a,c,b
    ]], {
        -- <selectA-2.13>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.13>
    })

test:do_execsql_test(
    "selectA-2.14",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY b,a,c
    ]], {
        -- <selectA-2.14>
        "", "C", "c", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z", 1, "a", "a", 9.9, "b", "B", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.14>
    })

test:do_execsql_test(
    "selectA-2.15",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY b COLLATE NOCASE,a,c
    ]], {
        -- <selectA-2.15>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.15>
    })

test:do_execsql_test(
    "selectA-2.16",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY b COLLATE NOCASE DESC,a,c
    ]], {
        -- <selectA-2.16>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-2.16>
    })

test:do_execsql_test(
    "selectA-2.17",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY c,b,a
    ]], {
        -- <selectA-2.17>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.17>
    })

test:do_execsql_test(
    "selectA-2.18",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY c,a,b
    ]], {
        -- <selectA-2.18>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.18>
    })

test:do_execsql_test(
    "selectA-2.19",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY c DESC,a,b
    ]], {
        -- <selectA-2.19>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-2.19>
    })

test:do_execsql_test(
    "selectA-2.20",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY c COLLATE BINARY DESC,a,b
    ]], {
        -- <selectA-2.20>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "abc", "e", "e", "", "C", "c", 1, "a", "a", "hare", "m", "M", "hello", "d", "D", 9.9, "b", "B"
        -- </selectA-2.20>
    })

test:do_execsql_test(
    "selectA-2.21",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY a,b,c
    ]], {
        -- <selectA-2.21>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.21>
    })

test:do_execsql_test(
    "selectA-2.22",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY a DESC,b,c
    ]], {
        -- <selectA-2.22>
        "hare", "m", "M", "abc", "e", "e", "mad", "Z", "z", "hello", "d", "D", 5200000.0, "X", "x", 9.9, "b", "B", 1, "a", "a", -23, "Y", "y", "", "C", "c", "", "U", "u"
        -- </selectA-2.22>
    })

test:do_execsql_test(
    "selectA-2.23",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY a,c,b
    ]], {
        -- <selectA-2.23>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.23>
    })

test:do_execsql_test(
    "selectA-2.24",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY b,a,c
    ]], {
        -- <selectA-2.24>
        "", "C", "c", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z", 1, "a", "a", 9.9, "b", "B", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.24>
    })

test:do_execsql_test(
    "selectA-2.25",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY b COLLATE NOCASE,a,c
    ]], {
        -- <selectA-2.25>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.25>
    })

test:do_execsql_test(
    "selectA-2.26",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY b COLLATE NOCASE DESC,a,c
    ]], {
        -- <selectA-2.26>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-2.26>
    })

test:do_execsql_test(
    "selectA-2.27",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY c,b,a
    ]], {
        -- <selectA-2.27>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.27>
    })

test:do_execsql_test(
    "selectA-2.28",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY c,a,b
    ]], {
        -- <selectA-2.28>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.28>
    })

test:do_execsql_test(
    "selectA-2.29",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY c DESC,a,b
    ]], {
        -- <selectA-2.29>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-2.29>
    })

test:do_execsql_test(
    "selectA-2.30",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY c COLLATE BINARY DESC,a,b
    ]], {
        -- <selectA-2.30>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "abc", "e", "e", "", "C", "c", 1, "a", "a", "hare", "m", "M", "hello", "d", "D", 9.9, "b", "B"
        -- </selectA-2.30>
    })

test:do_execsql_test(
    "selectA-2.31",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY a,b,c
    ]], {
        -- <selectA-2.31>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.31>
    })

test:do_execsql_test(
    "selectA-2.32",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY a DESC,b,c
    ]], {
        -- <selectA-2.32>
        "hare", "m", "M", "abc", "e", "e", "mad", "Z", "z", "hello", "d", "D", 5200000.0, "X", "x", 9.9, "b", "B", 1, "a", "a", -23, "Y", "y", "", "C", "c", "", "U", "u"
        -- </selectA-2.32>
    })

test:do_execsql_test(
    "selectA-2.33",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY a,c,b
    ]], {
        -- <selectA-2.33>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.33>
    })

test:do_execsql_test(
    "selectA-2.34",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY b,a,c
    ]], {
        -- <selectA-2.34>
        "", "C", "c", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z", 1, "a", "a", 9.9, "b", "B", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.34>
    })

test:do_execsql_test(
    "selectA-2.35",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY y COLLATE NOCASE,x,z
    ]], {
        -- <selectA-2.35>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.35>
    })

test:do_execsql_test(
    "selectA-2.36",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY y COLLATE NOCASE DESC,x,z
    ]], {
        -- <selectA-2.36>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-2.36>
    })

test:do_execsql_test(
    "selectA-2.37",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY c,b,a
    ]], {
        -- <selectA-2.37>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.37>
    })

test:do_execsql_test(
    "selectA-2.38",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY c,a,b
    ]], {
        -- <selectA-2.38>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.38>
    })

test:do_execsql_test(
    "selectA-2.39",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY c DESC,a,b
    ]], {
        -- <selectA-2.39>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-2.39>
    })

test:do_execsql_test(
    "selectA-2.40",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY z COLLATE BINARY DESC,x,y
    ]], {
        -- <selectA-2.40>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "abc", "e", "e", "", "C", "c", 1, "a", "a", "hare", "m", "M", "hello", "d", "D", 9.9, "b", "B"
        -- </selectA-2.40>
    })

test:do_execsql_test(
    "selectA-2.41",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY a,b,c
    ]], {
        -- <selectA-2.41>
        "", "C", "c", 1, "a", "a", 9.9, "b", "B"
        -- </selectA-2.41>
    })

test:do_execsql_test(
    "selectA-2.42",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY a,b,c
    ]], {
        -- <selectA-2.42>
        "hello", "d", "D", "abc", "e", "e"
        -- </selectA-2.42>
    })

test:do_execsql_test(
    "selectA-2.43",
    [[
        SELECT a,b,c FROM t1 WHERE b>='d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY a,b,c
    ]], {
        -- <selectA-2.43>
        "hello", "d", "D", "abc", "e", "e"
        -- </selectA-2.43>
    })

test:do_execsql_test(
    "selectA-2.44",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY a,b,c
    ]], {
        -- <selectA-2.44>
        "hello", "d", "D", "abc", "e", "e"
        -- </selectA-2.44>
    })

test:do_execsql_test(
    "selectA-2.45",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY a,b,c
    ]], {
        -- <selectA-2.45>
        "", "C", "c", 1, "a", "a", 9.9, "b", "B"
        -- </selectA-2.45>
    })

test:do_execsql_test(
    "selectA-2.46",
    [[
        SELECT a,b,c FROM t1 WHERE b<'d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY a,b,c
    ]], {
        -- <selectA-2.46>
        "", "C", "c", 1, "a", "a", 9.9, "b", "B"
        -- </selectA-2.46>
    })

test:do_execsql_test(
    "selectA-2.47",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY a DESC
    ]], {
        -- <selectA-2.47>
        9.9, "b", "B", 1, "a", "a", "", "C", "c"
        -- </selectA-2.47>
    })

test:do_execsql_test(
    "selectA-2.48",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY a DESC
    ]], {
        -- <selectA-2.48>
        "abc", "e", "e", "hello", "d", "D"
        -- </selectA-2.48>
    })

test:do_execsql_test(
    "selectA-2.49",
    [[
        SELECT a,b,c FROM t1 WHERE b>='d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY a DESC
    ]], {
        -- <selectA-2.49>
        "abc", "e", "e", "hello", "d", "D"
        -- </selectA-2.49>
    })

test:do_execsql_test(
    "selectA-2.50",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY a DESC
    ]], {
        -- <selectA-2.50>
        "abc", "e", "e", "hello", "d", "D"
        -- </selectA-2.50>
    })

test:do_execsql_test(
    "selectA-2.51",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY a DESC
    ]], {
        -- <selectA-2.51>
        9.9, "b", "B", 1, "a", "a", "", "C", "c"
        -- </selectA-2.51>
    })

test:do_execsql_test(
    "selectA-2.52",
    [[
        SELECT a,b,c FROM t1 WHERE b<'d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY a DESC
    ]], {
        -- <selectA-2.52>
        9.9, "b", "B", 1, "a", "a", "", "C", "c"
        -- </selectA-2.52>
    })

test:do_execsql_test(
    "selectA-2.53",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY b, a DESC
    ]], {
        -- <selectA-2.53>
        "", "C", "c", 1, "a", "a", 9.9, "b", "B"
        -- </selectA-2.53>
    })

test:do_execsql_test(
    "selectA-2.54",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY b
    ]], {
        -- <selectA-2.54>
        "hello", "d", "D", "abc", "e", "e"
        -- </selectA-2.54>
    })

test:do_execsql_test(
    "selectA-2.55",
    [[
        SELECT a,b,c FROM t1 WHERE b>='d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY b DESC, c
    ]], {
        -- <selectA-2.55>
        "abc", "e", "e", "hello", "d", "D"
        -- </selectA-2.55>
    })

test:do_execsql_test(
    "selectA-2.56",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY b, c DESC, a
    ]], {
        -- <selectA-2.56>
        "hello", "d", "D", "abc", "e", "e"
        -- </selectA-2.56>
    })

test:do_execsql_test(
    "selectA-2.57",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY b COLLATE NOCASE
    ]], {
        -- <selectA-2.57>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-2.57>
    })

test:do_execsql_test(
    "selectA-2.58",
    [[
        SELECT a,b,c FROM t1 WHERE b<'d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY b
    ]], {
        -- <selectA-2.58>
        "", "C", "c", 1, "a", "a", 9.9, "b", "B"
        -- </selectA-2.58>
    })

test:do_execsql_test(
    "selectA-2.59",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY c, a DESC
    ]], {
        -- <selectA-2.59>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-2.59>
    })

test:do_execsql_test(
    "selectA-2.60",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY c
    ]], {
        -- <selectA-2.60>
        "hello", "d", "D", "abc", "e", "e"
        -- </selectA-2.60>
    })

test:do_execsql_test(
    "selectA-2.61",
    [[
        SELECT a,b,c FROM t1 WHERE b>='d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY c COLLATE BINARY, b DESC, c, a, b, c, a, b, c
    ]], {
        -- <selectA-2.61>
        "hello", "d", "D", "abc", "e", "e"
        -- </selectA-2.61>
    })

test:do_execsql_test(
    "selectA-2.62",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY c DESC, a
    ]], {
        -- <selectA-2.62>
        "abc", "e", "e", "hello", "d", "D"
        -- </selectA-2.62>
    })

test:do_execsql_test(
    "selectA-2.63",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY c COLLATE NOCASE
    ]], {
        -- <selectA-2.63>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-2.63>
    })

test:do_execsql_test(
    "selectA-2.64",
    [[
        SELECT a,b,c FROM t1 WHERE b<'d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY c
    ]], {
        -- <selectA-2.64>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-2.64>
    })

test:do_execsql_test(
    "selectA-2.65",
    [[
        SELECT a,b,c FROM t3 INTERSECT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY c COLLATE NOCASE
    ]], {
        -- <selectA-2.65>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-2.65>
    })

test:do_execsql_test(
    "selectA-2.66",
    [[
        SELECT a,b,c FROM t1 WHERE b<'d' INTERSECT SELECT a,b,c FROM t3
        ORDER BY c
    ]], {
        -- <selectA-2.66>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-2.66>
    })

test:do_execsql_test(
    "selectA-2.67",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t3 WHERE b<'d'
        ORDER BY c DESC, a
    ]], {
        -- <selectA-2.67>
        "abc", "e", "e", "hello", "d", "D"
        -- </selectA-2.67>
    })

test:do_execsql_test(
    "selectA-2.68",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b<'d'
        INTERSECT SELECT a,b,c FROM t3
        EXCEPT SELECT b,c,a FROM t3
        ORDER BY c DESC, a
    ]], {
        -- <selectA-2.68>
        "abc", "e", "e", "hello", "d", "D"
        -- </selectA-2.68>
    })

test:do_execsql_test(
    "selectA-2.69",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b<'d'
        INTERSECT SELECT a,b,c FROM t3
        EXCEPT SELECT b,c,a FROM t3
        ORDER BY c COLLATE NOCASE
    ]], {
        -- <selectA-2.69>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-2.69>
    })

test:do_execsql_test(
    "selectA-2.70",
    [[
        SELECT a,b,c FROM t1 WHERE b<'d' INTERSECT SELECT a,b,c FROM t1
        INTERSECT SELECT a,b,c FROM t3
        EXCEPT SELECT b,c,a FROM t3
        ORDER BY c
    ]], {
        -- <selectA-2.70>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-2.70>
    })

test:do_execsql_test(
    "selectA-2.71",
    [[
        SELECT a,b,c FROM t1 WHERE b<'d'
        INTERSECT SELECT a,b,c FROM t1
        INTERSECT SELECT a,b,c FROM t3
        EXCEPT SELECT b,c,a FROM t3
        INTERSECT SELECT a,b,c FROM t1
        EXCEPT SELECT x,y,z FROM t2
        INTERSECT SELECT a,b,c FROM t3
        EXCEPT SELECT y,x,z FROM t2
        INTERSECT SELECT a,b,c FROM t1
        EXCEPT SELECT c,b,a FROM t3
        ORDER BY c
    ]], {
        -- <selectA-2.71>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-2.71>
    })

test:do_execsql_test(
    "selectA-2.72",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY a,b,c
    ]], {
        -- <selectA-2.72>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.72>
    })

test:do_execsql_test(
    "selectA-2.73",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY a DESC,b,c
    ]], {
        -- <selectA-2.73>
        "hare", "m", "M", "abc", "e", "e", "mad", "Z", "z", "hello", "d", "D", 5200000.0, "X", "x", 9.9, "b", "B", 1, "a", "a", -23, "Y", "y", "", "C", "c", "", "U", "u"
        -- </selectA-2.73>
    })

test:do_execsql_test(
    "selectA-2.74",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY a,c,b
    ]], {
        -- <selectA-2.74>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.74>
    })

test:do_execsql_test(
    "selectA-2.75",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY b,a,c
    ]], {
        -- <selectA-2.75>
        "", "C", "c", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z", 1, "a", "a", 9.9, "b", "B", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.75>
    })

test:do_execsql_test(
    "selectA-2.76",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY b COLLATE NOCASE,a,c
    ]], {
        -- <selectA-2.76>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.76>
    })

test:do_execsql_test(
    "selectA-2.77",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY b COLLATE NOCASE DESC,a,c
    ]], {
        -- <selectA-2.77>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-2.77>
    })

test:do_execsql_test(
    "selectA-2.78",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY c,b,a
    ]], {
        -- <selectA-2.78>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.78>
    })

test:do_execsql_test(
    "selectA-2.79",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY c,a,b
    ]], {
        -- <selectA-2.79>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.79>
    })

test:do_execsql_test(
    "selectA-2.80",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY c DESC,a,b
    ]], {
        -- <selectA-2.80>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-2.80>
    })

test:do_execsql_test(
    "selectA-2.81",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY c COLLATE BINARY DESC,a,b
    ]], {
        -- <selectA-2.81>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "abc", "e", "e", "", "C", "c", 1, "a", "a", "hare", "m", "M", "hello", "d", "D", 9.9, "b", "B"
        -- </selectA-2.81>
    })

test:do_execsql_test(
    "selectA-2.82",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY a,b,c
    ]], {
        -- <selectA-2.82>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.82>
    })

test:do_execsql_test(
    "selectA-2.83",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY a DESC,b,c
    ]], {
        -- <selectA-2.83>
        "hare", "m", "M", "abc", "e", "e", "mad", "Z", "z", "hello", "d", "D", 5200000.0, "X", "x", 9.9, "b", "B", 1, "a", "a", -23, "Y", "y", "", "C", "c", "", "U", "u"
        -- </selectA-2.83>
    })

test:do_execsql_test(
    "selectA-2.84",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY a,c,b
    ]], {
        -- <selectA-2.84>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.84>
    })

test:do_execsql_test(
    "selectA-2.85",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY b,a,c
    ]], {
        -- <selectA-2.85>
        "", "C", "c", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z", 1, "a", "a", 9.9, "b", "B", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-2.85>
    })

test:do_execsql_test(
    "selectA-2.86",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY y COLLATE NOCASE,x,z
    ]], {
        -- <selectA-2.86>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.86>
    })

test:do_execsql_test(
    "selectA-2.87",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY y COLLATE NOCASE DESC,x,z
    ]], {
        -- <selectA-2.87>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-2.87>
    })

test:do_execsql_test(
    "selectA-2.88",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY c,b,a
    ]], {
        -- <selectA-2.88>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.88>
    })

test:do_execsql_test(
    "selectA-2.89",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY c,a,b
    ]], {
        -- <selectA-2.89>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-2.89>
    })

test:do_execsql_test(
    "selectA-2.90",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY c DESC,a,b
    ]], {
        -- <selectA-2.90>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-2.90>
    })

test:do_execsql_test(
    "selectA-2.91",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY z COLLATE BINARY DESC,x,y
    ]], {
        -- <selectA-2.91>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "abc", "e", "e", "", "C", "c", 1, "a", "a", "hare", "m", "M", "hello", "d", "D", 9.9, "b", "B"
        -- </selectA-2.91>
    })

test:do_execsql_test(
    "selectA-2.92",
    [[
        SELECT x,y,z FROM t2
        INTERSECT SELECT a,b,c FROM t3
        EXCEPT SELECT c,b,a FROM t1
        UNION SELECT a,b,c FROM t3
        INTERSECT SELECT a,b,c FROM t3
        EXCEPT SELECT c,b,a FROM t1
        UNION SELECT a,b,c FROM t3
        ORDER BY y COLLATE NOCASE DESC,x,z
    ]], {
        -- <selectA-2.92>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-2.92>
    })

test:do_execsql_test(
    "selectA-2.93",
    [[
        SELECT upper((SELECT c FROM t1 UNION SELECT z FROM t2 ORDER BY 1));
    ]], {
        -- <selectA-2.93>
        "A"
        -- </selectA-2.93>
    })

test:do_execsql_test(
    "selectA-2.94",
    [[
        SELECT lower((SELECT c FROM t1 UNION ALL SELECT z FROM t2 ORDER BY 1));
    ]], {
        -- <selectA-2.94>
        "a"
        -- </selectA-2.94>
    })

test:do_execsql_test(
    "selectA-2.95",
    [[
        SELECT lower((SELECT c FROM t1 INTERSECT SELECT z FROM t2 ORDER BY 1));
    ]], {
        -- <selectA-2.95>
        ""
        -- </selectA-2.95>
    })

test:do_execsql_test(
    "selectA-2.96",
    [[
        SELECT lower((SELECT z FROM t2 EXCEPT SELECT c FROM t1 ORDER BY 1));
    ]], {
        -- <selectA-2.96>
        "m"
        -- </selectA-2.96>
    })

test:do_execsql_test(
    "selectA-3.0",
    [[
        CREATE UNIQUE INDEX t1a ON t1(a);
        CREATE UNIQUE INDEX t1b ON t1(b);
        CREATE UNIQUE INDEX t1c ON t1(c);
        CREATE UNIQUE INDEX t2x ON t2(x);
        CREATE UNIQUE INDEX t2y ON t2(y);
        CREATE UNIQUE INDEX t2z ON t2(z);
        SELECT name FROM _index WHERE name LIKE 't%';
    ]], {
        -- <selectA-3.0>
        "t1a", "t1b", "t1c", "t2x", "t2y", "t2z"
        -- </selectA-3.0>
    })

test:do_execsql_test(
    "selectA-3.1",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY a,b,c
    ]], {
        -- <selectA-3.1>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.1>
    })

test:do_test(
    "selectA-3.1.1",
    function()
        -- Ticket #3314
        return test:execsql [[
            SELECT t1.a,b,t1.c FROM t1 UNION ALL SELECT x,y,z FROM t2
            ORDER BY a,t1.b,t1.c
        ]]
    end, {
        -- <selectA-3.1.1>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.1.1>
    })

test:do_execsql_test(
    "selectA-3.2",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY a DESC,b,c
    ]], {
        -- <selectA-3.2>
        "hare", "m", "M", "abc", "e", "e", "mad", "Z", "z", "hello", "d", "D", 5200000.0, "X", "x", 9.9, "b", "B", 1, "a", "a", -23, "Y", "y", "", "C", "c", "", "U", "u"
        -- </selectA-3.2>
    })

test:do_execsql_test(
    "selectA-3.3",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY a,c,b
    ]], {
        -- <selectA-3.3>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.3>
    })

test:do_execsql_test(
    "selectA-3.4",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY b,a,c
    ]], {
        -- <selectA-3.4>
        "", "C", "c", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z", 1, "a", "a", 9.9, "b", "B", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.4>
    })

test:do_execsql_test(
    "selectA-3.5",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY b COLLATE NOCASE,a,c
    ]], {
        -- <selectA-3.5>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.5>
    })

test:do_execsql_test(
    "selectA-3.6",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY b COLLATE NOCASE DESC,a,c
    ]], {
        -- <selectA-3.6>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-3.6>
    })

-- MUST_WORK_TEST wrong order by result
if 0>0 then
test:do_execsql_test(
    "selectA-3.7",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY c,b,a
    ]], {
        -- <selectA-3.7>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.7>
    })

test:do_execsql_test(
    "selectA-3.8",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY c,a,b
    ]], {
        -- <selectA-3.8>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.8>
    })

test:do_execsql_test(
    "selectA-3.9",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY c DESC,a,b
    ]], {
        -- <selectA-3.9>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-3.9>
    })
end
test:do_execsql_test(
    "selectA-3.10",
    [[
        SELECT a,b,c FROM t1 UNION ALL SELECT x,y,z FROM t2
        ORDER BY c COLLATE BINARY DESC,a,b
    ]], {
        -- <selectA-3.10>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "abc", "e", "e", "", "C", "c", 1, "a", "a", "hare", "m", "M", "hello", "d", "D", 9.9, "b", "B"
        -- </selectA-3.10>
    })

test:do_execsql_test(
    "selectA-3.11",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY a,b,c
    ]], {
        -- <selectA-3.11>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.11>
    })

test:do_execsql_test(
    "selectA-3.12",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY a DESC,b,c
    ]], {
        -- <selectA-3.12>
        "hare", "m", "M", "abc", "e", "e", "mad", "Z", "z", "hello", "d", "D", 5200000.0, "X", "x", 9.9, "b", "B", 1, "a", "a", -23, "Y", "y", "", "C", "c", "", "U", "u"
        -- </selectA-3.12>
    })

test:do_execsql_test(
    "selectA-3.13",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY a,c,b
    ]], {
        -- <selectA-3.13>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.13>
    })

test:do_execsql_test(
    "selectA-3.14",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY b,a,c
    ]], {
        -- <selectA-3.14>
        "", "C", "c", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z", 1, "a", "a", 9.9, "b", "B", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.14>
    })

test:do_execsql_test(
    "selectA-3.15",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY b COLLATE NOCASE,a,c
    ]], {
        -- <selectA-3.15>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.15>
    })

test:do_execsql_test(
    "selectA-3.16",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY b COLLATE NOCASE DESC,a,c
    ]], {
        -- <selectA-3.16>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-3.16>
    })

-- MUST_WORK_TEST wrong order by result
if 0>0 then
test:do_execsql_test(
    "selectA-3.17",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY c,b,a
    ]], {
        -- <selectA-3.17>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.17>
    })

test:do_execsql_test(
    "selectA-3.18",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY c,a,b
    ]], {
        -- <selectA-3.18>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.18>
    })

test:do_execsql_test(
    "selectA-3.19",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY c DESC,a,b
    ]], {
        -- <selectA-3.19>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-3.19>
    })
end
test:do_execsql_test(
    "selectA-3.20",
    [[
        SELECT x,y,z FROM t2 UNION ALL SELECT a,b,c FROM t1
        ORDER BY c COLLATE BINARY DESC,a,b
    ]], {
        -- <selectA-3.20>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "abc", "e", "e", "", "C", "c", 1, "a", "a", "hare", "m", "M", "hello", "d", "D", 9.9, "b", "B"
        -- </selectA-3.20>
    })

test:do_execsql_test(
    "selectA-3.21",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY a,b,c
    ]], {
        -- <selectA-3.21>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.21>
    })

test:do_execsql_test(
    "selectA-3.22",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY a DESC,b,c
    ]], {
        -- <selectA-3.22>
        "hare", "m", "M", "abc", "e", "e", "mad", "Z", "z", "hello", "d", "D", 5200000.0, "X", "x", 9.9, "b", "B", 1, "a", "a", -23, "Y", "y", "", "C", "c", "", "U", "u"
        -- </selectA-3.22>
    })

test:do_execsql_test(
    "selectA-3.23",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY a,c,b
    ]], {
        -- <selectA-3.23>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.23>
    })

test:do_execsql_test(
    "selectA-3.24",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY b,a,c
    ]], {
        -- <selectA-3.24>
        "", "C", "c", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z", 1, "a", "a", 9.9, "b", "B", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.24>
    })

test:do_execsql_test(
    "selectA-3.25",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY b COLLATE NOCASE,a,c
    ]], {
        -- <selectA-3.25>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.25>
    })

test:do_execsql_test(
    "selectA-3.26",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY b COLLATE NOCASE DESC,a,c
    ]], {
        -- <selectA-3.26>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-3.26>
    })

-- MUST_WORK_TEST wrong order by result
if 0>0 then
test:do_execsql_test(
    "selectA-3.27",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY c,b,a
    ]], {
        -- <selectA-3.27>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.27>
    })

test:do_execsql_test(
    "selectA-3.28",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY c,a,b
    ]], {
        -- <selectA-3.28>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.28>
    })

test:do_execsql_test(
    "selectA-3.29",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY c DESC,a,b
    ]], {
        -- <selectA-3.29>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-3.29>
    })
end

test:do_execsql_test(
    "selectA-3.30",
    [[
        SELECT a,b,c FROM t1 UNION SELECT x,y,z FROM t2
        ORDER BY c COLLATE BINARY DESC,a,b
    ]], {
        -- <selectA-3.30>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "abc", "e", "e", "", "C", "c", 1, "a", "a", "hare", "m", "M", "hello", "d", "D", 9.9, "b", "B"
        -- </selectA-3.30>
    })

test:do_execsql_test(
    "selectA-3.31",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY a,b,c
    ]], {
        -- <selectA-3.31>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.31>
    })

test:do_execsql_test(
    "selectA-3.32",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY a DESC,b,c
    ]], {
        -- <selectA-3.32>
        "hare", "m", "M", "abc", "e", "e", "mad", "Z", "z", "hello", "d", "D", 5200000.0, "X", "x", 9.9, "b", "B", 1, "a", "a", -23, "Y", "y", "", "C", "c", "", "U", "u"
        -- </selectA-3.32>
    })

test:do_execsql_test(
    "selectA-3.33",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY a,c,b
    ]], {
        -- <selectA-3.33>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.33>
    })

test:do_execsql_test(
    "selectA-3.34",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY b,a,c
    ]], {
        -- <selectA-3.34>
        "", "C", "c", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z", 1, "a", "a", 9.9, "b", "B", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.34>
    })

test:do_execsql_test(
    "selectA-3.35",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY y COLLATE NOCASE,x,z
    ]], {
        -- <selectA-3.35>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.35>
    })

test:do_execsql_test(
    "selectA-3.36",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY y COLLATE NOCASE DESC,x,z
    ]], {
        -- <selectA-3.36>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-3.36>
    })

-- MUST_WORK_TEST wrong order by result
if 0>0 then
test:do_execsql_test(
    "selectA-3.37",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY c,b,a
    ]], {
        -- <selectA-3.37>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.37>
    })

test:do_execsql_test(
    "selectA-3.38",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY c,a,b
    ]], {
        -- <selectA-3.38>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.38>
    })

test:do_execsql_test(
    "selectA-3.39",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY c DESC,a,b
    ]], {
        -- <selectA-3.39>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-3.39>
    })
end

test:do_execsql_test(
    "selectA-3.40",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t1
        ORDER BY z COLLATE BINARY DESC,x,y
    ]], {
        -- <selectA-3.40>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "abc", "e", "e", "", "C", "c", 1, "a", "a", "hare", "m", "M", "hello", "d", "D", 9.9, "b", "B"
        -- </selectA-3.40>
    })

test:do_execsql_test(
    "selectA-3.41",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY a,b,c
    ]], {
        -- <selectA-3.41>
        "", "C", "c", 1, "a", "a", 9.9, "b", "B"
        -- </selectA-3.41>
    })

test:do_execsql_test(
    "selectA-3.42",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY a,b,c
    ]], {
        -- <selectA-3.42>
        "hello", "d", "D", "abc", "e", "e"
        -- </selectA-3.42>
    })

test:do_execsql_test(
    "selectA-3.43",
    [[
        SELECT a,b,c FROM t1 WHERE b>='d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY a,b,c
    ]], {
        -- <selectA-3.43>
        "hello", "d", "D", "abc", "e", "e"
        -- </selectA-3.43>
    })

test:do_execsql_test(
    "selectA-3.44",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY a,b,c
    ]], {
        -- <selectA-3.44>
        "hello", "d", "D", "abc", "e", "e"
        -- </selectA-3.44>
    })

test:do_execsql_test(
    "selectA-3.45",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY a,b,c
    ]], {
        -- <selectA-3.45>
        "", "C", "c", 1, "a", "a", 9.9, "b", "B"
        -- </selectA-3.45>
    })

test:do_execsql_test(
    "selectA-3.46",
    [[
        SELECT a,b,c FROM t1 WHERE b<'d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY a,b,c
    ]], {
        -- <selectA-3.46>
        "", "C", "c", 1, "a", "a", 9.9, "b", "B"
        -- </selectA-3.46>
    })

test:do_execsql_test(
    "selectA-3.47",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY a DESC
    ]], {
        -- <selectA-3.47>
        9.9, "b", "B", 1, "a", "a", "", "C", "c"
        -- </selectA-3.47>
    })

test:do_execsql_test(
    "selectA-3.48",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY a DESC
    ]], {
        -- <selectA-3.48>
        "abc", "e", "e", "hello", "d", "D"
        -- </selectA-3.48>
    })

test:do_execsql_test(
    "selectA-3.49",
    [[
        SELECT a,b,c FROM t1 WHERE b>='d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY a DESC
    ]], {
        -- <selectA-3.49>
        "abc", "e", "e", "hello", "d", "D"
        -- </selectA-3.49>
    })

test:do_execsql_test(
    "selectA-3.50",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY a DESC
    ]], {
        -- <selectA-3.50>
        "abc", "e", "e", "hello", "d", "D"
        -- </selectA-3.50>
    })

test:do_execsql_test(
    "selectA-3.51",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY a DESC
    ]], {
        -- <selectA-3.51>
        9.9, "b", "B", 1, "a", "a", "", "C", "c"
        -- </selectA-3.51>
    })

test:do_execsql_test(
    "selectA-3.52",
    [[
        SELECT a,b,c FROM t1 WHERE b<'d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY a DESC
    ]], {
        -- <selectA-3.52>
        9.9, "b", "B", 1, "a", "a", "", "C", "c"
        -- </selectA-3.52>
    })

test:do_execsql_test(
    "selectA-3.53",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY b, a DESC
    ]], {
        -- <selectA-3.53>
        "", "C", "c", 1, "a", "a", 9.9, "b", "B"
        -- </selectA-3.53>
    })

test:do_execsql_test(
    "selectA-3.54",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY b
    ]], {
        -- <selectA-3.54>
        "hello", "d", "D", "abc", "e", "e"
        -- </selectA-3.54>
    })

test:do_execsql_test(
    "selectA-3.55",
    [[
        SELECT a,b,c FROM t1 WHERE b>='d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY b DESC, c
    ]], {
        -- <selectA-3.55>
        "abc", "e", "e", "hello", "d", "D"
        -- </selectA-3.55>
    })

test:do_execsql_test(
    "selectA-3.56",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY b, c DESC, a
    ]], {
        -- <selectA-3.56>
        "hello", "d", "D", "abc", "e", "e"
        -- </selectA-3.56>
    })

test:do_execsql_test(
    "selectA-3.57",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY b COLLATE NOCASE
    ]], {
        -- <selectA-3.57>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-3.57>
    })

test:do_execsql_test(
    "selectA-3.58",
    [[
        SELECT a,b,c FROM t1 WHERE b<'d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY b
    ]], {
        -- <selectA-3.58>
        "", "C", "c", 1, "a", "a", 9.9, "b", "B"
        -- </selectA-3.58>
    })

-- MUST_WORK_TEST wrong order by result
if 0>0 then
test:do_execsql_test(
    "selectA-3.59",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY c, a DESC
    ]], {
        -- <selectA-3.59>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-3.59>
    })
end

test:do_execsql_test(
    "selectA-3.60",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b>='d'
        ORDER BY c
    ]], {
        -- <selectA-3.60>
        "hello", "d", "D", "abc", "e", "e"
        -- </selectA-3.60>
    })

test:do_execsql_test(
    "selectA-3.61",
    [[
        SELECT a,b,c FROM t1 WHERE b>='d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY c COLLATE BINARY, b DESC, c, a, b, c, a, b, c
    ]], {
        -- <selectA-3.61>
        "hello", "d", "D", "abc", "e", "e"
        -- </selectA-3.61>
    })

test:do_execsql_test(
    "selectA-3.62",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY c DESC, a
    ]], {
        -- <selectA-3.62>
        "abc", "e", "e", "hello", "d", "D"
        -- </selectA-3.62>
    })

test:do_execsql_test(
    "selectA-3.63",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY c COLLATE NOCASE
    ]], {
        -- <selectA-3.63>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-3.63>
    })

-- MUST_WORK_TEST collation
if 0>0 then
test:do_execsql_test(
    "selectA-3.64",
    [[
        explain SELECT a,b,c FROM t1 WHERE b<'d' INTERSECT SELECT a,b,c FROM t1
        ORDER BY c
    ]], {
        -- <selectA-3.64>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-3.64>
    })

test:do_execsql_test(
    "selectA-3.65",
    [[
        SELECT a,b,c FROM t3 INTERSECT SELECT a,b,c FROM t1 WHERE b<'d'
        ORDER BY c COLLATE NOCASE
    ]], {
        -- <selectA-3.65>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-3.65>
    })

test:do_execsql_test(
"selectA-3.66",
[[
        SELECT a,b,c FROM t1 WHERE b<'d' INTERSECT SELECT a,b,c FROM t3
        ORDER BY c
    ]], {
-- <selectA-3.66>
1, "a", "a", 9.9, "b", "B", "", "C", "c"
-- </selectA-3.66>
})
end

test:do_execsql_test(
    "selectA-3.67",
    [[
            SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t3 WHERE b<'d'
            ORDER BY c DESC, a
        ]], {
-- <selectA-3.67>
        "abc", "e", "e", "hello", "d", "D"
        -- </selectA-3.67>
    })

test:do_execsql_test(
    "selectA-3.68",
    [[
        SELECT a,b,c FROM t1 EXCEPT SELECT a,b,c FROM t1 WHERE b<'d'
        INTERSECT SELECT a,b,c FROM t3
        EXCEPT SELECT b,c,a FROM t3
        ORDER BY c DESC, a
    ]], {
        -- <selectA-3.68>
        "abc", "e", "e", "hello", "d", "D"
        -- </selectA-3.68>
    })

test:do_execsql_test(
    "selectA-3.69",
    [[
        SELECT a,b,c FROM t1 INTERSECT SELECT a,b,c FROM t1 WHERE b<'d'
        INTERSECT SELECT a,b,c FROM t3
        EXCEPT SELECT b,c,a FROM t3
        ORDER BY c COLLATE NOCASE
    ]], {
        -- <selectA-3.69>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-3.69>
    })

-- MUST_WORK_TEST
if 0>0 then
test:do_execsql_test(
    "selectA-3.70",
    [[
        SELECT a,b,c FROM t1 WHERE b<'d' INTERSECT SELECT a,b,c FROM t1
        INTERSECT SELECT a,b,c FROM t3
        EXCEPT SELECT b,c,a FROM t3
        ORDER BY c
    ]], {
        -- <selectA-3.70>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-3.70>
    })

test:do_execsql_test(
    "selectA-3.71",
    [[
        SELECT a,b,c FROM t1 WHERE b<'d'
        INTERSECT SELECT a,b,c FROM t1
        INTERSECT SELECT a,b,c FROM t3
        EXCEPT SELECT b,c,a FROM t3
        INTERSECT SELECT a,b,c FROM t1
        EXCEPT SELECT x,y,z FROM t2
        INTERSECT SELECT a,b,c FROM t3
        EXCEPT SELECT y,x,z FROM t2
        INTERSECT SELECT a,b,c FROM t1
        EXCEPT SELECT c,b,a FROM t3
        ORDER BY c
    ]], {
        -- <selectA-3.71>
        1, "a", "a", 9.9, "b", "B", "", "C", "c"
        -- </selectA-3.71>
    })
end

test:do_execsql_test(
    "selectA-3.72",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY a,b,c
    ]], {
        -- <selectA-3.72>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.72>
    })

test:do_execsql_test(
    "selectA-3.73",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY a DESC,b,c
    ]], {
        -- <selectA-3.73>
        "hare", "m", "M", "abc", "e", "e", "mad", "Z", "z", "hello", "d", "D", 5200000.0, "X", "x", 9.9, "b", "B", 1, "a", "a", -23, "Y", "y", "", "C", "c", "", "U", "u"
        -- </selectA-3.73>
    })

test:do_execsql_test(
    "selectA-3.74",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY a,c,b
    ]], {
        -- <selectA-3.74>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.74>
    })

test:do_execsql_test(
    "selectA-3.75",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY b,a,c
    ]], {
        -- <selectA-3.75>
        "", "C", "c", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z", 1, "a", "a", 9.9, "b", "B", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.75>
    })

test:do_execsql_test(
    "selectA-3.76",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY b COLLATE NOCASE,a,c
    ]], {
        -- <selectA-3.76>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.76>
    })

test:do_execsql_test(
    "selectA-3.77",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY b COLLATE NOCASE DESC,a,c
    ]], {
        -- <selectA-3.77>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-3.77>
    })

test:do_execsql_test(
    "selectA-3.78",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY c,b,a
    ]], {
        -- <selectA-3.78>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.78>
    })

test:do_execsql_test(
    "selectA-3.79",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY c,a,b
    ]], {
        -- <selectA-3.79>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.79>
    })

test:do_execsql_test(
    "selectA-3.80",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY c DESC,a,b
    ]], {
        -- <selectA-3.80>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-3.80>
    })

test:do_execsql_test(
    "selectA-3.81",
    [[
        SELECT a,b,c FROM t3 UNION SELECT x,y,z FROM t2
        ORDER BY c COLLATE BINARY DESC,a,b
    ]], {
        -- <selectA-3.81>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "abc", "e", "e", "", "C", "c", 1, "a", "a", "hare", "m", "M", "hello", "d", "D", 9.9, "b", "B"
        -- </selectA-3.81>
    })

test:do_execsql_test(
    "selectA-3.82",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY a,b,c
    ]], {
        -- <selectA-3.82>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.82>
    })

test:do_execsql_test(
    "selectA-3.83",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY a DESC,b,c
    ]], {
        -- <selectA-3.83>
        "hare", "m", "M", "abc", "e", "e", "mad", "Z", "z", "hello", "d", "D", 5200000.0, "X", "x", 9.9, "b", "B", 1, "a", "a", -23, "Y", "y", "", "C", "c", "", "U", "u"
        -- </selectA-3.83>
    })

test:do_execsql_test(
    "selectA-3.84",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY a,c,b
    ]], {
        -- <selectA-3.84>
        "", "C", "c", "", "U", "u", -23, "Y", "y", 1, "a", "a", 9.9, "b", "B", 5200000.0, "X", "x", "hello", "d", "D", "mad", "Z", "z", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.84>
    })

test:do_execsql_test(
    "selectA-3.85",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY b,a,c
    ]], {
        -- <selectA-3.85>
        "", "C", "c", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z", 1, "a", "a", 9.9, "b", "B", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M"
        -- </selectA-3.85>
    })

test:do_execsql_test(
    "selectA-3.86",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY y COLLATE NOCASE,x,z
    ]], {
        -- <selectA-3.86>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.86>
    })

test:do_execsql_test(
    "selectA-3.87",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY y COLLATE NOCASE DESC,x,z
    ]], {
        -- <selectA-3.87>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-3.87>
    })

test:do_execsql_test(
    "selectA-3.88",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY c,b,a
    ]], {
        -- <selectA-3.88>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.88>
    })

test:do_execsql_test(
    "selectA-3.89",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY c,a,b
    ]], {
        -- <selectA-3.89>
        1, "a", "a", 9.9, "b", "B", "", "C", "c", "hello", "d", "D", "abc", "e", "e", "hare", "m", "M", "", "U", "u", 5200000.0, "X", "x", -23, "Y", "y", "mad", "Z", "z"
        -- </selectA-3.89>
    })

test:do_execsql_test(
    "selectA-3.90",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY c DESC,a,b
    ]], {
        -- <selectA-3.90>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-3.90>
    })

test:do_execsql_test(
    "selectA-3.91",
    [[
        SELECT x,y,z FROM t2 UNION SELECT a,b,c FROM t3
        ORDER BY z COLLATE BINARY DESC,x,y
    ]], {
        -- <selectA-3.91>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "abc", "e", "e", "", "C", "c", 1, "a", "a", "hare", "m", "M", "hello", "d", "D", 9.9, "b", "B"
        -- </selectA-3.91>
    })

test:do_execsql_test(
    "selectA-3.92",
    [[
        SELECT x,y,z FROM t2
        INTERSECT SELECT a,b,c FROM t3
        EXCEPT SELECT c,b,a FROM t1
        UNION SELECT a,b,c FROM t3
        INTERSECT SELECT a,b,c FROM t3
        EXCEPT SELECT c,b,a FROM t1
        UNION SELECT a,b,c FROM t3
        ORDER BY y COLLATE NOCASE DESC,x,z
    ]], {
        -- <selectA-3.92>
        "mad", "Z", "z", -23, "Y", "y", 5200000.0, "X", "x", "", "U", "u", "hare", "m", "M", "abc", "e", "e", "hello", "d", "D", "", "C", "c", 9.9, "b", "B", 1, "a", "a"
        -- </selectA-3.92>
    })

-- MUST_WORK_TEST
if 0>0 then
test:do_execsql_test(
    "selectA-3.93",
    [[
        SELECT upper((SELECT c FROM t1 UNION SELECT z FROM t2 ORDER BY 1));
    ]], {
        -- <selectA-3.93>
        "A"
        -- </selectA-3.93>
    })

test:do_execsql_test(
    "selectA-3.94",
    [[
        SELECT lower((SELECT c FROM t1 UNION ALL SELECT z FROM t2 ORDER BY 1));
    ]], {
        -- <selectA-3.94>
        "a"
        -- </selectA-3.94>
    })
end

test:do_execsql_test(
    "selectA-3.95",
    [[
        SELECT lower((SELECT c FROM t1 INTERSECT SELECT z FROM t2 ORDER BY 1));
    ]], {
        -- <selectA-3.95>
        ""
        -- </selectA-3.95>
    })

test:do_execsql_test(
    "selectA-3.96",
    [[
        SELECT lower((SELECT z FROM t2 EXCEPT SELECT c FROM t1 ORDER BY 1));
    ]], {
        -- <selectA-3.96>
        "m"
        -- </selectA-3.96>
    })

test:do_execsql_test(
    "selectA-3.97",
    [[
        SELECT upper((SELECT x FROM (
          SELECT x,y,z FROM t2
          INTERSECT SELECT a,b,c FROM t3
          EXCEPT SELECT c,b,a FROM t1
          UNION SELECT a,b,c FROM t3
          INTERSECT SELECT a,b,c FROM t3
          EXCEPT SELECT c,b,a FROM t1
          UNION SELECT a,b,c FROM t3
          ORDER BY y COLLATE NOCASE DESC,x,z)))
    ]], {
        -- <selectA-3.97>
        "MAD"
        -- </selectA-3.97>
    })

test:do_execsql_test(
    "selectA-3.98",
    [[
        WITH RECURSIVE
          xyz(n) AS (
            SELECT upper((SELECT x FROM (
              SELECT x,y,z FROM t2
              INTERSECT SELECT a,b,c FROM t3
              EXCEPT SELECT c,b,a FROM t1
              UNION SELECT a,b,c FROM t3
              INTERSECT SELECT a,b,c FROM t3
              EXCEPT SELECT c,b,a FROM t1
              UNION SELECT a,b,c FROM t3
              ORDER BY y COLLATE NOCASE DESC,x,z)))
            UNION ALL
            SELECT n || '+' FROM xyz WHERE length(n)<5
          )
        SELECT n FROM xyz ORDER BY +n;
    ]], {
        -- <selectA-3.98>
        "MAD", "MAD+", "MAD++"
        -- </selectA-3.98>
    })

---------------------------------------------------------------------------
-- At one point the following code exposed a temp register reuse problem.
--
-- MUST_WORK_TEST
-- TODO stored procedures are not supported by now
if (0 > 0)
 then
end
local function f(args)
    return 1
end

-- TODO stored procedures are not supported by now
--db("func", "f", "f")
test:do_execsql_test(
    "4.1.1",
    [[
        DROP TABLE IF EXISTS t4;
        DROP TABLE IF EXISTS t5;
        CREATE TABLE t4(id int primary key, a int, b);
        CREATE TABLE t5(id int primary key, c int, d);

        INSERT INTO t5 VALUES(0, 1, 'x');
        INSERT INTO t5 VALUES(1, 2, 'x');
        INSERT INTO t4 VALUES(0, 3, 'x');
        INSERT INTO t4 VALUES(1, 4, 'x');

        CREATE INDEX i1 ON t4(a);
        CREATE INDEX i2 ON t5(c);
    ]])

-- TODO stored procedures are not supported by now
--test:do_eqp_test(
--    "4.1.2",
--    [[
--        SELECT c, d FROM t5
--        UNION ALL
--        SELECT a, b FROM t4 WHERE f()==f()
--        ORDER BY 1,2
--    ]], {
--        -- <4.1.2>
--    {1, 0, 0, "SCAN TABLE t5 USING INDEX i2" },
--    {1, 0, 0, "USE TEMP B-TREE FOR RIGHT PART OF ORDER BY"},
--    {2, 0, 0, "SCAN TABLE t4 USING INDEX i1"},
--    {2, 0, 0, "USE TEMP B-TREE FOR RIGHT PART OF ORDER BY"},
--    {0, 0, 0, "COMPOUND SUBQUERIES 1 AND 2 (UNION ALL)"}
--        -- </4.1.2>
--    })

-- TODO stored procedures are not supported by now
--test:do_execsql_test(
--    "4.1.3",
--    [[
--        SELECT c, d FROM t5
--        UNION ALL
--        SELECT a, b FROM t4 WHERE f()==f()
--        ORDER BY 1,2
--    ]], {
--        -- <4.1.3>
--        1, "x", 2, "x", 3, "x", 4, "x"
--        -- </4.1.3>
--    })

test:do_execsql_test(
    "4.2.1",
    [[
        DROP TABLE IF EXISTS t6;
        DROP TABLE IF EXISTS t7;
        CREATE TABLE t6(id int primary key, a, b);
        CREATE TABLE t7(id int primary key, c, d);

        INSERT INTO t7 VALUES(0, 2, 9);
        INSERT INTO t6 VALUES(0, 3, 0);
        INSERT INTO t6 VALUES(1, 4, 1);
        INSERT INTO t7 VALUES(1, 5, 6);
        INSERT INTO t6 VALUES(2, 6, 0);
        INSERT INTO t7 VALUES(2, 7, 6);

        CREATE INDEX i6 ON t6(a);
        CREATE INDEX i7 ON t7(c);
    ]])

-- TODO stored procedures are not supported by now
--test:do_execsql_test(
--    "4.2.2",
--    [[
--        SELECT c, f(d,c,d,c,d) FROM t7
--        UNION ALL
--        SELECT a, b FROM t6
--        ORDER BY 1,2
--    ]], {
--        -- <4.2.2>
--        "/2 . 3 . 4 . 5 . 6 . 7 ./"
--        -- </4.2.2>
--    })

local function strip_rnd(explain)
    for i, val in ipairs(explain) do
        explain[i] = string.gsub(val, "sqlite_sq_[0123456789ABCDEF]*", "sqlite_sq")
    end
    return explain
end

local function do_same_test(tn, q1, args)
    local r2 = strip_rnd(test:execsql("EXPLAIN "..q1))
    for i, q in ipairs(args) do
        local tst = strip_rnd(test:execsql("EXPLAIN "..q))
        test:do_test(
            tn.."."..i,
            function()
                return tst
            end,
            r2)
    end
end

test:do_execsql_test(
    5.0,
    [[
        DROP TABLE IF EXISTS t8;
        DROP TABLE IF EXISTS t9;
        CREATE TABLE t8(id int primary key, a, b);
        CREATE TABLE t9(id int primary key, c, d);
    ]], {
        -- <5.0>
        
        -- </5.0>
    })

do_same_test(
    5.1,
    [[
      SELECT a, b FROM t8 INTERSECT SELECT c, d FROM t9 ORDER BY a;
    ]],
    { [[
      SELECT a, b FROM t8 INTERSECT SELECT c, d FROM t9 ORDER BY t8.a;
    ]], [[
      SELECT a, b FROM t8 INTERSECT SELECT c, d FROM t9 ORDER BY 1;
    ]], [[
      SELECT a, b FROM t8 INTERSECT SELECT c, d FROM t9 ORDER BY c;
    ]], [[
      SELECT a, b FROM t8 INTERSECT SELECT c, d FROM t9 ORDER BY t9.c;
    ]]})

do_same_test(
    5.2,
    [[
      SELECT a, b FROM t8 UNION SELECT c, d FROM t9 ORDER BY a COLLATE NOCASE
    ]],
    {[[
      SELECT a, b FROM t8 UNION SELECT c, d FROM t9 ORDER BY t8.a COLLATE NOCASE
    ]], [[
      SELECT a, b FROM t8 UNION SELECT c, d FROM t9 ORDER BY 1 COLLATE NOCASE
    ]], [[
      SELECT a, b FROM t8 UNION SELECT c, d FROM t9 ORDER BY c COLLATE NOCASE
    ]], [[
      SELECT a, b FROM t8 UNION SELECT c, d FROM t9 ORDER BY t9.c COLLATE NOCASE
    ]]})
    do_same_test(5.3,
        [[
      SELECT a, b FROM t8 EXCEPT SELECT c, d FROM t9 ORDER BY b, c COLLATE NOCASE
    ]],
        {[[
      SELECT a, b FROM t8 EXCEPT SELECT c, d FROM t9 ORDER BY 2, 1 COLLATE NOCASE
    ]], [[
      SELECT a, b FROM t8 EXCEPT SELECT c, d FROM t9 ORDER BY d, a COLLATE NOCASE
    ]], [[
      SELECT a, b FROM t8 EXCEPT SELECT c, d FROM t9 ORDER BY t9.d, c COLLATE NOCASE
    ]], [[
      SELECT a, b FROM t8 EXCEPT SELECT c, d FROM t9 ORDER BY d, t8.a COLLATE NOCASE
    ]]})

test:do_catchsql_test(
    5.4,
    [[
        SELECT a, b FROM t8 UNION SELECT c, d FROM t9 ORDER BY a+b COLLATE NOCASE
    ]], {
        -- <5.4>
        1, "1st ORDER BY term does not match any column in the result set"
        -- </5.4>
    })

test:finish_test()

