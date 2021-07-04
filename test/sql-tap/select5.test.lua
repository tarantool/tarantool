#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(46)

--!./tcltestrunner.lua
-- 2001 September 15
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
-- focus of this file is testing aggregate functions and the
-- GROUP BY and HAVING clauses of SELECT statements.
--
-- $Id: select5.test,v 1.20 2008/08/21 14:15:59 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Build some test data
--
test:execsql [[
    DROP TABLE IF EXISTS t1;
    CREATE TABLE t1(x int primary key, y int);
    START TRANSACTION;
]]
local j
for i = 1, 31, 1 do
    j = 0
    while math.pow(2, j) < i do
        j = j+1
    end
    test:execsql(string.format("INSERT INTO t1 VALUES(%s,%s)", 32 - i, 10 - j))
end
test:execsql [[
    COMMIT
]]
test:do_execsql_test(
    "select5-1.0",
    [[
        SELECT DISTINCT y FROM t1 ORDER BY y
    ]], {
        -- <select5-1.0>
        5, 6, 7, 8, 9, 10
        -- </select5-1.0>
    })

-- Sort by an aggregate function.
--
test:do_execsql_test(
    "select5-1.1",
    [[
        SELECT y, count(*) FROM t1 GROUP BY y ORDER BY y
    ]], {
        -- <select5-1.1>
        5, 15, 6, 8, 7, 4, 8, 2, 9, 1, 10, 1
        -- </select5-1.1>
    })

test:do_execsql_test(
    "select5-1.2",
    [[
        SELECT y, count(*) FROM t1 GROUP BY y ORDER BY count(*), y
    ]], {
        -- <select5-1.2>
        9, 1, 10, 1, 8, 2, 7, 4, 6, 8, 5, 15
        -- </select5-1.2>
    })

test:do_execsql_test(
    "select5-1.3",
    [[
        SELECT count(*), y FROM t1 GROUP BY y ORDER BY count(*), y
    ]], {
        -- <select5-1.3>
        1, 9, 1, 10, 2, 8, 4, 7, 8, 6, 15, 5
        -- </select5-1.3>
    })

-- Some error messages associated with aggregates and GROUP BY
--
test:do_catchsql_test(
    "select5-2.1.1",
    [[
        SELECT y, count(*) FROM t1 GROUP BY z ORDER BY y
    ]], {
        -- <select5-2.1.1>
        1, "Can’t resolve field 'Z'"
        -- </select5-2.1.1>
    })

test:do_catchsql_test(
    "select5-2.2",
    [[
        SELECT y, count(*) FROM t1 GROUP BY z(y) ORDER BY y
    ]], {
        -- <select5-2.2>
        1, "Function 'Z' does not exist"
        -- </select5-2.2>
    })

test:do_catchsql_test(
    "select5-2.3",
    [[
        SELECT y, count(*) FROM t1 GROUP BY y HAVING count(*)<3 ORDER BY y
    ]], {
        -- <select5-2.3>
        0, {8, 2, 9, 1, 10, 1}
        -- </select5-2.3>
    })

test:do_catchsql_test(
    "select5-2.4",
    [[
        SELECT y, count(*) FROM t1 GROUP BY y HAVING z(y)<3 ORDER BY y
    ]], {
        -- <select5-2.4>
        1, "Function 'Z' does not exist"
        -- </select5-2.4>
    })

test:do_catchsql_test(
    "select5-2.5",
    [[
        SELECT y, count(*) FROM t1 GROUP BY y HAVING count(*)<z ORDER BY y
    ]], {
        -- <select5-2.5>
        1, "Can’t resolve field 'Z'"
        -- </select5-2.5>
    })

-- Get the Agg function to rehash in vdbe.c
--
test:do_execsql_test(
    "select5-3.1",
    [[
        SELECT x, count(*), avg(y) FROM t1 GROUP BY x HAVING x<4 ORDER BY x
    ]], {
        -- <select5-3.1>
        1, 1, 5.0, 2, 1, 5.0, 3, 1, 5.0
        -- </select5-3.1>
    })

-- Run various aggregate functions when the count is zero.
--
test:do_execsql_test(
    "select5-4.1",
    [[
        SELECT avg(x) FROM t1 WHERE x>100
    ]], {
        -- <select5-4.1>
        ""
        -- </select5-4.1>
    })

test:do_execsql_test(
    "select5-4.2",
    [[
        SELECT count(x) FROM t1 WHERE x>100
    ]], {
        -- <select5-4.2>
        0
        -- </select5-4.2>
    })

test:do_execsql_test(
    "select5-4.3",
    [[
        SELECT min(x) FROM t1 WHERE x>100
    ]], {
        -- <select5-4.3>
        ""
        -- </select5-4.3>
    })

test:do_execsql_test(
    "select5-4.4",
    [[
        SELECT max(x) FROM t1 WHERE x>100
    ]], {
        -- <select5-4.4>
        ""
        -- </select5-4.4>
    })

test:do_execsql_test(
    "select5-4.5",
    [[
        SELECT sum(x) FROM t1 WHERE x>100
    ]], {
        -- <select5-4.5>
        ""
        -- </select5-4.5>
    })

-- Some tests for queries with a GROUP BY clause but no aggregate functions.
--
-- Note: The query in test cases 5.1 through 5.5 are not legal SQL. So if the
-- implementation changes in the future and it returns different results,
-- this is not such a big deal.
--
test:do_execsql_test(
    "select5-5.1",
    [[
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t2(id int primary key, a INT, b INT, c INT);
        INSERT INTO t2 VALUES(0, 1, 2, 3);
        INSERT INTO t2 VALUES(1, 1, 4, 5);
        INSERT INTO t2 VALUES(2, 6, 4, 7);
        CREATE INDEX t2_idx ON t2(a);
    ]], {
        -- <select5-5.1>

        -- </select5-5.1>
    })

test:do_execsql_test(
    "select5-5.2",
    [[
        SELECT a FROM t2 GROUP BY a;
    ]], {
        -- <select5-5.2>
        1, 6
        -- </select5-5.2>
    })

test:do_execsql_test(
    "select5-5.3",
    [[
        SELECT a FROM t2 WHERE a>2 GROUP BY a;
    ]], {
        -- <select5-5.3>
        6
        -- </select5-5.3>
    })

test:do_execsql_test(
    "select5-5.4",
    [[
        SELECT a, b FROM t2 GROUP BY a, b;
    ]], {
        -- <select5-5.4>
        1, 2, 1, 4, 6, 4
        -- </select5-5.4>
    })

test:do_execsql_test(
    "select5-5.5",
    [[
        SELECT a, b FROM t2 GROUP BY a;
    ]], {
        -- <select5-5.5>
        1, 4, 6, 4
        -- </select5-5.5>
    })

-- Test rendering of columns for the GROUP BY clause.
--
test:do_execsql_test(
    "select5-5.11",
    [[
        SELECT max(c), b*a, b, a FROM t2 GROUP BY b*a, b, a
    ]], {
        -- <select5-5.11>
        3, 2, 2, 1, 5, 4, 4, 1, 7, 24, 4, 6
        -- </select5-5.11>
    })

-- NULL compare equal to each other for the purposes of processing
-- the GROUP BY clause.
--
test:do_execsql_test(
    "select5-6.1",
    [[
        DROP TABLE IF EXISTS t3;
        CREATE TABLE t3(x INT primary key,y INT);
        INSERT INTO t3 VALUES(1,NULL);
        INSERT INTO t3 VALUES(2,NULL);
        INSERT INTO t3 VALUES(3,4);
        SELECT count(x), y FROM t3 GROUP BY y ORDER BY 1
    ]], {
        -- <select5-6.1>
        1, 4, 2, ""
        -- </select5-6.1>
    })

test:do_execsql_test(
    "select5-6.2",
    [[
        DROP TABLE IF EXISTS t4;
        CREATE TABLE t4(id int primary key, x INT,y INT,z INT);
        INSERT INTO t4 VALUES(0,1,2,NULL);
        INSERT INTO t4 VALUES(1,2,3,NULL);
        INSERT INTO t4 VALUES(2,3,NULL,5);
        INSERT INTO t4 VALUES(3,4,NULL,6);
        INSERT INTO t4 VALUES(4,4,NULL,6);
        INSERT INTO t4 VALUES(5,5,NULL,NULL);
        INSERT INTO t4 VALUES(6,5,NULL,NULL);
        INSERT INTO t4 VALUES(7,6,7,8);
        SELECT max(x), count(x), y, z FROM t4 GROUP BY y, z ORDER BY 1
    ]], {
        -- <select5-6.2>
        1, 1, 2, "", 2, 1, 3, "", 3, 1, "", 5, 4, 2, "", 6, 5, 2, "", "", 6, 1, 7, 8
        -- </select5-6.2>
    })

test:do_execsql_test(
    "select5-7.2",
    [[
        SELECT count(*), count(x) as cnt FROM t4 GROUP BY y ORDER BY cnt;
    ]], {
        -- <select5-7.2>
        1, 1, 1, 1, 1, 1, 5, 5
        -- </select5-7.2>
    })

-- See ticket #3324.
--
test:do_execsql_test(
    "select5-8.1",
    [[
        DROP TABLE IF EXISTS t8a;
        DROP TABLE IF EXISTS t8b;
        CREATE TABLE t8a(id int primary key,a TEXT,b INT);
        CREATE TABLE t8b(rowid int primary key, x INT);
        INSERT INTO t8a VALUES(0, 'one', 1);
        INSERT INTO t8a VALUES(1, 'one', 2);
        INSERT INTO t8a VALUES(2, 'two', 3);
        INSERT INTO t8a VALUES(3, 'one', NULL);
        INSERT INTO t8b(rowid,x) VALUES(1,111);
        INSERT INTO t8b(rowid,x) VALUES(2,222);
        INSERT INTO t8b(rowid,x) VALUES(3,333);
        SELECT a, count(b) FROM t8a, t8b WHERE b=t8b.rowid GROUP BY a ORDER BY a;
    ]], {
        -- <select5-8.1>
        "one", 2, "two", 1
        -- </select5-8.1>
    })

test:do_execsql_test(
    "select5-8.2",
    [[
        SELECT a, count(b) FROM t8a, t8b WHERE b=+t8b.rowid GROUP BY a ORDER BY a;
    ]], {
        -- <select5-8.2>
        "one", 2, "two", 1
        -- </select5-8.2>
    })

test:do_execsql_test(
    "select5-8.3",
    [[
        SELECT t8a.a, count(t8a.b) FROM t8a, t8b WHERE t8a.b=t8b.rowid
         GROUP BY 1 ORDER BY 1;
    ]], {
        -- <select5-8.3>
        "one", 2, "two", 1
        -- </select5-8.3>
    })

test:do_execsql_test(
    "select5-8.4",
    [[
        SELECT a, count(*) FROM t8a, t8b WHERE b=+t8b.rowid GROUP BY a ORDER BY a;
    ]], {
        -- <select5-8.4>
        "one", 2, "two", 1
        -- </select5-8.4>
    })

test:do_execsql_test(
    "select5-8.5",
    [[
        SELECT a, count(b) FROM t8a, t8b WHERE b<x GROUP BY a ORDER BY a;
    ]], {
        -- <select5-8.5>
        "one", 6, "two", 3
        -- </select5-8.5>
    })

test:do_execsql_test(
    "select5-8.6",
    [[
        SELECT a, count(t8a.b) FROM t8a, t8b WHERE b=t8b.rowid
         GROUP BY a ORDER BY 2;
    ]], {
        -- <select5-8.6>
        "two", 1, "one", 2
        -- </select5-8.6>
    })

test:do_execsql_test(
    "select5-8.7",
    [[
        SELECT a, count(b) FROM t8a, t8b GROUP BY a ORDER BY 2;
    ]], {
        -- <select5-8.7>
        "two", 3, "one", 6
        -- </select5-8.7>
    })

test:do_execsql_test(
    "select5-8.8",
    [[
        SELECT a, count(*) FROM t8a, t8b GROUP BY a ORDER BY 2;
    ]], {
        -- <select5-8.8>
        "two", 3, "one", 9
        -- </select5-8.8>
    })

--
-- gh-2364: Support HAVING without GROUP BY clause.
--
test:do_catchsql_test(
    "select5-9.1",
    [[
        CREATE TABLE te40 (s1 INT, s2 INT, PRIMARY KEY (s1,s2));
        INSERT INTO te40 VALUES (1,1);
        INSERT INTO te40 VALUES (2,2);
        SELECT s1 FROM te40 HAVING s1 = 1;
    ]], {
    -- <select5-9.1>
    1, "Failed to execute SQL statement: HAVING argument must appear in the GROUP BY clause or be used in an aggregate function"
    -- </select5-9.1>
})

test:do_catchsql_test(
    "select5-9.2",
    [[
        SELECT SUM(s1) FROM te40 HAVING s1 = 2;
    ]], {
    -- <select5-9.2>
    1, "Failed to execute SQL statement: HAVING argument must appear in the GROUP BY clause or be used in an aggregate function"
    -- </select5-9.2>
})

test:do_catchsql_test(
    "select5-9.3",
    [[
        SELECT s1 FROM te40 HAVING SUM(s1) = 2;
    ]], {
    -- <select5-9.3>
    1, "Failed to execute SQL statement: HAVING argument must appear in the GROUP BY clause or be used in an aggregate function"
    -- </select5-9.3>
})

test:do_execsql_test(
    "select5-9.4",
    [[
        SELECT SUM(s1) FROM te40 HAVING SUM(s1) > 0;
    ]], {
    -- <select5-9.4>
    3
    -- </select5-9.4>
})

test:do_execsql_test(
    "select5-9.5",
    [[
        SELECT MIN(s1) FROM te40 HAVING SUM(s1) > 0;
    ]], {
    -- <select5-9.5>
    1
    -- </select5-9.5>
})

test:do_execsql_test(
    "select5-9.6",
    [[
        SELECT SUM(s1) FROM te40 HAVING SUM(s1) < 0;
    ]],
    -- <select5-9.6>
    {}
    -- </select5-9.6>
)

test:do_catchsql_test(
    "select5-9.7",
    [[
        SELECT SUM(s1),s2 FROM te40 HAVING SUM(s1) > 0;
    ]], {
    -- <select5-9.7>
    1, "Failed to execute SQL statement: HAVING argument must appear in the GROUP BY clause or be used in an aggregate function"
    -- </select5-9.7>
})

test:do_catchsql_test(
    "select5-9.8",
    [[
        SELECT SUM(s1) FROM te40 HAVING SUM(s1) > 0 and s2 > 0;
    ]], {
    -- <select5-9.8>
    1, "Failed to execute SQL statement: HAVING argument must appear in the GROUP BY clause or be used in an aggregate function"
    -- </select5-9.8>
})

test:do_execsql_test(
    "select5-9.9",
    [[
        SELECT SUM(s1) FROM te40 HAVING SUM(s1) > 0 and SUM(s2) > 0;
    ]], {
    -- <select5-9.9>
    3
    -- </select5-9.9>
})

test:do_execsql_test(
    "select5-9.10",
    [[
        SELECT 1 FROM te40 HAVING SUM(s1) > 0;
    ]], {
    -- <select5-9.10>
    1
    -- </select5-9.10>
})

test:do_execsql_test(
    "select5-9.11",
    [[
        SELECT -1 FROM te40 HAVING SUM(s1) > 0;
    ]], {
    -- <select5-9.11>
    -1
    -- </select5-9.11>
})

test:do_execsql_test(
    "select5-9.12",
    [[
        SELECT NULL FROM te40 HAVING SUM(s1) > 0;
    ]], {
    -- <select5-9.12>
    ""
    -- </select5-9.12>
})

-- gh-3932: bytecode is not emmited if aggregate is placed only
-- in HAVING clause.
--
test:do_execsql_test(
    "select5-9.13",
    [[
        SELECT 1 FROM te40 HAVING SUM(s1) < 0;
    ]], {
    -- <select5-9.13>
    -- </select5-9.13>
})

test:do_catchsql_test(
    "select5-9.13.2",
    [[
            CREATE TABLE jj (s1 INT, s2 VARCHAR(1), PRIMARY KEY(s1));
            INSERT INTO jj VALUES(1, 'A'), (2, 'a');
            SELECT 1 FROM jj HAVING avg(s2) = 1 AND avg(s2) = 0;
    ]], {
    -- <select5-9.13.2>
    1, "Type mismatch: can not convert string('A') to number"
    -- </select5-9.13.2>
})

test:finish_test()

