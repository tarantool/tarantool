#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(35)

--!./tcltestrunner.lua
-- 2012 Sept 27
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
-- focus of this file is testing that the optimizations that disable
-- ORDER BY clauses when the natural order of a query is correct.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

-- MUST_WORK_TEST
-- # Generate test data for a join.  Verify that the join gets the
-- # correct answer.
-- #
test:do_test(
    1.0,
    function()
        return test:execsql [[
            CREATE TABLE album(
              aid INT PRIMARY KEY,
              title TEXT UNIQUE NOT NULL
            );
            CREATE TABLE track(

              tid INTEGER PRIMARY KEY,
              aid INTEGER NOT NULL REFERENCES album,
              tn INTEGER NOT NULL,
              name TEXT,
              UNIQUE(aid, tn)
            );
            START TRANSACTION;
            INSERT INTO album VALUES(1, '1-one'), (2, '2-two'), (3, '3-three');
            INSERT INTO track VALUES
                (1, 1, 1, 'one-a'),
                (2, 2, 2, 'two-b'),
                (3, 3, 3, 'three-c'),
                (4, 1, 3, 'one-c'),
                (5, 2, 1, 'two-a'),
                (6, 3, 1, 'three-a');
            COMMIT;
        ]]
    end, {
        -- <1.0>

        -- </1.0>
    })

test:do_test(
    "1.1a",
    function()
        return test:execsql [[
            SELECT name FROM album JOIN track USING (aid) ORDER BY title, tn
        ]]
    end, {
        -- <1.1a>
        "one-a", "one-c", "two-a", "two-b", "three-a", "three-c"
        -- </1.1a>
    })

-- # Verify that the ORDER BY clause is optimized out
-- #
test:do_test(
    "1.1b",
    function()
        return test:execsql [[
            EXPLAIN QUERY PLAN
            SELECT name FROM album CROSS JOIN track USING (aid) ORDER BY title, tn
        ]]
    end, {
        -- <1.1b>
        "~/ORDER BY/"
        -- </1.1b>
    })

-- ORDER BY optimized out
-- # The same query with ORDER BY clause optimization disabled via + operators
-- # should give exactly the same answer.
-- #
test:do_test(
    "1.2a",
    function()
        return test:execsql [[
            SELECT name FROM album JOIN track USING (aid) ORDER BY +title, +tn
        ]]
    end, {
        -- <1.2a>
        "one-a", "one-c", "two-a", "two-b", "three-a", "three-c"
        -- </1.2a>
    })

-- # The output is sorted manually in this case.
-- #
test:do_test(
    "1.2b",
    function()
        return test:execsql [[
            EXPLAIN QUERY PLAN
            SELECT name FROM album JOIN track USING (aid) ORDER BY +title, +tn
        ]]
    end, {
        -- <1.2b>
        "/ORDER BY/"
        -- </1.2b>
    })

-- separate sorting pass due to "+" on ORDER BY terms
-- # The same query with ORDER BY optimizations turned off via built-in test.
-- #
-- test:do_test(
--     "1.3a",
--     function()
--         X(90, "X!cmd", [=[["optimization_control","db","order-by-idx-join","0"]]=])
--         db("cache", "flush")
--         return test:execsql [[
--             SELECT name FROM album JOIN track USING (aid) ORDER BY title, tn
--         ]]
--     end, {
--         -- <1.3a>
--         "one-a", "one-c", "two-a", "two-b", "three-a", "three-c"
--         -- </1.3a>
--     })

-- Tarantool: we're unable to switch off the optimization
--  Comment test so far
-- test:do_test(
--     "1.3b",
--     function()
--         return test:execsql [[
--             EXPLAIN QUERY PLAN
--             SELECT name FROM album JOIN track USING (aid) ORDER BY title, tn
--         ]]
--     end, {
--         -- <1.3b>
--         "/ORDER BY/"
--         -- </1.3b>
--     })

-- separate sorting pass due to disabled optimization
-- X(101, "X!cmd", [=[["optimization_control","db","all","1"]]=])
-- db("cache", "flush")
-- # Reverse order sorts
-- #
test:do_test(
    "1.4a",
    function()
        return test:execsql [[
            SELECT name FROM album JOIN track USING (aid) ORDER BY title DESC, tn
        ]]
    end, {
        -- <1.4a>
        "three-a", "three-c", "two-a", "two-b", "one-a", "one-c"
        -- </1.4a>
    })

test:do_test(
    "1.4b",
    function()
        return test:execsql [[
            SELECT name FROM album JOIN track USING (aid) ORDER BY +title DESC, +tn
        ]]
    end, {
        -- <1.4b>
        "three-a", "three-c", "two-a", "two-b", "one-a", "one-c"
        -- </1.4b>
    })

-- verify same order after sorting
test:do_test(
    "1.4c",
    function()
        return test:execsql [[
            EXPLAIN QUERY PLAN
            SELECT name FROM album JOIN track USING (aid) ORDER BY title DESC, tn
        ]]
    end, {
        -- <1.4c>
        "~/ORDER BY/"
        -- </1.4c>
    })

-- ORDER BY suppressed due to uniqueness constraints
test:do_test(
    "1.5a",
    function()
        return test:execsql [[
            SELECT name FROM album JOIN track USING (aid) ORDER BY title, tn DESC
        ]]
    end, {
        -- <1.5a>
        "one-c", "one-a", "two-b", "two-a", "three-c", "three-a"
        -- </1.5a>
    })

test:do_test(
    "1.5b",
    function()
        return test:execsql [[
            SELECT name FROM album JOIN track USING (aid) ORDER BY +title, +tn DESC
        ]]
    end, {
        -- <1.5b>
        "one-c", "one-a", "two-b", "two-a", "three-c", "three-a"
        -- </1.5b>
    })

-- verify same order after sorting
test:do_test(
    "1.5c",
    function()
        return test:execsql [[
            EXPLAIN QUERY PLAN
            SELECT name FROM album JOIN track USING (aid) ORDER BY title, tn DESC
        ]]
    end, {
        -- <1.5c>
        "~/ORDER BY/"
        -- </1.5c>
    })

-- ORDER BY suppressed due to uniqueness constraints
test:do_test(
    "1.6a",
    function()
        return test:execsql [[
            SELECT name FROM album CROSS JOIN track USING (aid)
             ORDER BY title DESC, tn DESC
        ]]
    end, {
        -- <1.6a>
        "three-c", "three-a", "two-b", "two-a", "one-c", "one-a"
        -- </1.6a>
    })

test:do_test(
    "1.6b",
    function()
        return test:execsql [[
            SELECT name FROM album CROSS JOIN track USING (aid)
             ORDER BY +title DESC, +tn DESC
        ]]
    end, {
        -- <1.6b>
        "three-c", "three-a", "two-b", "two-a", "one-c", "one-a"
        -- </1.6b>
    })

-- verify same order after sorting
test:do_test(
    "1.6c",
    function()
        return test:execsql [[
            EXPLAIN QUERY PLAN
            SELECT name FROM album CROSS JOIN track USING (aid)
             ORDER BY title DESC, tn DESC
        ]]
    end, {
        -- <1.6c>
        "~/ORDER BY/"
        -- </1.6c>
    })

-- ORDER BY
-- # Reconstruct the test data to use indices rather than integer primary keys.
-- #
-- do_test 2.0 {
--   db eval {
--     BEGIN;
--     DROP TABLE album;
--     DROP TABLE track;
--     CREATE TABLE album(
--       aid INT PRIMARY KEY,
--       title TEXT NOT NULL
--     );
--     CREATE INDEX album_i1 ON album(title, aid);
--     CREATE TABLE track(
--       aid INTEGER NOT NULL REFERENCES album,
--       tn INTEGER NOT NULL,
--       name TEXT,
--       UNIQUE(aid, tn)
--     );
--     INSERT INTO album VALUES(1, '1-one'), (20, '2-two'), (3, '3-three');
--     INSERT INTO track VALUES
--         (1,  1, 'one-a'),
--         (20, 2, 'two-b'),
--         (3,  3, 'three-c'),
--         (1,  3, 'one-c'),
--         (20, 1, 'two-a'),
--         (3,  1, 'three-a');
--     COMMIT;
--   }
-- } {}
-- do_test 2.1a {
--   db eval {
--     SELECT name FROM album JOIN track USING (aid) ORDER BY title, tn
--   }
-- } {one-a one-c two-a two-b three-a three-c}
-- # Verify that the ORDER BY clause is optimized out
-- #
-- do_test 2.1b {
--   db eval {
--     EXPLAIN QUERY PLAN
--     SELECT name FROM album JOIN track USING (aid) ORDER BY title, tn
--   }
-- } {/ORDER BY/}  ;# ORDER BY required because of missing aid term in ORDER BY
-- do_test 2.1c {
--   db eval {
--     SELECT name FROM album JOIN track USING (aid) ORDER BY title, aid, tn
--   }
-- } {one-a one-c two-a two-b three-a three-c}
-- do_test 2.1d {
--   db eval {
--     EXPLAIN QUERY PLAN
--     SELECT name FROM album JOIN track USING (aid) ORDER BY title, aid, tn
--   }
-- } {/ORDER BY/}  ;# ORDER BY required in this case
-- # The same query with ORDER BY clause optimization disabled via + operators
-- # should give exactly the same answer.
-- #
-- do_test 2.2a {
--   db eval {
--     SELECT name FROM album JOIN track USING (aid) ORDER BY +title, +tn
--   }
-- } {one-a one-c two-a two-b three-a three-c}
-- # The output is sorted manually in this case.
-- #
-- do_test 2.2b {
--   db eval {
--     EXPLAIN QUERY PLAN
--     SELECT name FROM album JOIN track USING (aid) ORDER BY +title, +tn
--   }
-- } {/ORDER BY/}   ;# separate sorting pass due to "+" on ORDER BY terms
-- # The same query with ORDER BY optimizations turned off via built-in test.
-- #
-- do_test 2.3a {
--   optimization_control db order-by-idx-join 0
--   db cache flush
--   db eval {
--     SELECT name FROM album JOIN track USING (aid) ORDER BY title, tn
--   }
-- } {one-a one-c two-a two-b three-a three-c}
-- do_test 2.3b {
--   db eval {
--     EXPLAIN QUERY PLAN
--     SELECT name FROM album JOIN track USING (aid) ORDER BY title, tn
--   }
-- } {/ORDER BY/}   ;# separate sorting pass due to disabled optimization
-- optimization_control db all 1
-- db cache flush
-- # Reverse order sorts
-- #
-- do_test 2.4a {
--   db eval {
--     SELECT name FROM album JOIN track USING (aid) ORDER BY title DESC, tn
--   }
-- } {three-a three-c two-a two-b one-a one-c}
-- do_test 2.4b {
--   db eval {
--     SELECT name FROM album JOIN track USING (aid) ORDER BY +title DESC, +tn
--   }
-- } {three-a three-c two-a two-b one-a one-c}  ;# verify same order after sorting
-- do_test 2.4c {
--   db eval {
--     EXPLAIN QUERY PLAN
--     SELECT name FROM album JOIN track USING (aid) ORDER BY title DESC, tn
--   }
-- } {/ORDER BY/}  ;# separate sorting pass due to mixed DESC/ASC
-- do_test 2.5a {
--   db eval {
--     SELECT name FROM album JOIN track USING (aid) ORDER BY title, tn DESC
--   }
-- } {one-c one-a two-b two-a three-c three-a}
-- do_test 2.5b {
--   db eval {
--     SELECT name FROM album JOIN track USING (aid) ORDER BY +title, +tn DESC
--   }
-- } {one-c one-a two-b two-a three-c three-a}  ;# verify same order after sorting
-- do_test 2.5c {
--   db eval {
--     EXPLAIN QUERY PLAN
--     SELECT name FROM album JOIN track USING (aid) ORDER BY title, tn DESC
--   }
-- } {/ORDER BY/}  ;# separate sorting pass due to mixed ASC/DESC
-- do_test 2.6a {
--   db eval {
--     SELECT name FROM album JOIN track USING (aid) ORDER BY title DESC, tn DESC
--   }
-- } {three-c three-a two-b two-a one-c one-a}
-- do_test 2.6b {
--   db eval {
--     SELECT name FROM album JOIN track USING (aid) ORDER BY +title DESC, +tn DESC
--   }
-- } {three-c three-a two-b two-a one-c one-a}  ;# verify same order after sorting
-- do_test 2.6c {
--   db eval {
--     EXPLAIN QUERY PLAN
--     SELECT name FROM album JOIN track USING (aid) ORDER BY title DESC, tn DESC
--   }
-- } {/ORDER BY/}  ;# ORDER BY required
-- # Generate another test dataset, but this time using mixed ASC/DESC indices.
-- #
test:do_test(
    3.0,
    function()
        return test:execsql [[
            DROP TABLE track;
            DROP TABLE album;
            CREATE TABLE album(
              aid INT PRIMARY KEY,
              title TEXT UNIQUE NOT NULL
            );
            CREATE TABLE track(
              tid INTEGER PRIMARY KEY,
              aid INTEGER NOT NULL REFERENCES album,
              tn INTEGER NOT NULL,
              name TEXT,
              UNIQUE(aid ASC, tn DESC)
            );
            START TRANSACTION;
            INSERT INTO album VALUES(1, '1-one'), (2, '2-two'), (3, '3-three');
            INSERT INTO track VALUES
                (1, 1, 1, 'one-a'),
                (2, 2, 2, 'two-b'),
                (3, 3, 3, 'three-c'),
                (4, 1, 3, 'one-c'),
                (5, 2, 1, 'two-a'),
                (6, 3, 1, 'three-a');
            COMMIT;
        ]]
    end, {
        -- <3.0>

        -- </3.0>
    })

test:do_test(
    "3.1a",
    function()
        return test:execsql [[
            SELECT name FROM album CROSS JOIN track USING (aid) ORDER BY title, tn DESC
        ]]
    end, {
        -- <3.1a>
        "one-c", "one-a", "two-b", "two-a", "three-c", "three-a"
        -- </3.1a>
    })

-- # Verify that the ORDER BY clause is optimized out
-- #
test:do_test(
    "3.1b",
    function()
        return test:execsql [[
            EXPLAIN QUERY PLAN
            SELECT name FROM album CROSS JOIN track USING (aid) ORDER BY title, tn DESC
        ]]
    end, {
        -- <3.1b>
        "~/ORDER BY/"
        -- </3.1b>
    })

-- ORDER BY optimized out
-- # The same query with ORDER BY clause optimization disabled via + operators
-- # should give exactly the same answer.
-- #
test:do_test(
    "3.2a",
    function()
        return test:execsql [[
            SELECT name FROM album JOIN track USING (aid) ORDER BY +title, +tn DESC
        ]]
    end, {
        -- <3.2a>
        "one-c", "one-a", "two-b", "two-a", "three-c", "three-a"
        -- </3.2a>
    })

-- # The output is sorted manually in this case.
-- #
test:do_test(
    "3.2b",
    function()
        return test:execsql [[
            EXPLAIN QUERY PLAN
            SELECT name FROM album JOIN track USING (aid) ORDER BY +title, +tn DESC
        ]]
    end, {
        -- <3.2b>
        "/ORDER BY/"
        -- </3.2b>
    })

-- separate sorting pass due to "+" on ORDER BY terms
-- # The same query with ORDER BY optimizations turned off via built-in test.
-- #
test:do_test(
    "3.3a",
    function()
        -- X(374, "X!cmd", [=[["optimization_control","db","order-by-idx-join","0"]]=])
        -- db("cache", "flush")
        return test:execsql [[
            SELECT name FROM album JOIN track USING (aid) ORDER BY title, tn DESC
        ]]
    end, {
        -- <3.3a>
        "one-c", "one-a", "two-b", "two-a", "three-c", "three-a"
        -- </3.3a>
    })

-- Tarantool: we're unable to switch off the optimization
--  Comment test so far
-- test:do_test(
--     "3.3b",
--     function()
--         return test:execsql [[
--             EXPLAIN QUERY PLAN
--             SELECT name FROM album JOIN track USING (aid) ORDER BY title, tn DESC
--         ]]
--     end, {
--         -- <3.3b>
--         "/ORDER BY/"
--         -- </3.3b>
--     })

-- separate sorting pass due to disabled optimization
-- X(385, "X!cmd", [=[["optimization_control","db","all","1"]]=])
-- db("cache", "flush")
-- # Without the mixed ASC/DESC on ORDER BY
-- #
test:do_test(
    "3.4a",
    function()
        return test:execsql [[
            SELECT name FROM album JOIN track USING (aid) ORDER BY title, tn
        ]]
    end, {
        -- <3.4a>
        "one-a", "one-c", "two-a", "two-b", "three-a", "three-c"
        -- </3.4a>
    })

test:do_test(
    "3.4b",
    function()
        return test:execsql [[
            SELECT name FROM album JOIN track USING (aid) ORDER BY +title, +tn
        ]]
    end, {
        -- <3.4b>
        "one-a", "one-c", "two-a", "two-b", "three-a", "three-c"
        -- </3.4b>
    })

-- verify same order after sorting
test:do_test(
    "3.4c",
    function()
        return test:execsql [[
            EXPLAIN QUERY PLAN
            SELECT name FROM album JOIN track USING (aid) ORDER BY title, tn
        ]]
    end, {
        -- <3.4c>
        "~/ORDER BY/"
        -- </3.4c>
    })

-- ORDER BY suppressed by uniqueness constraints
test:do_test(
    "3.5a",
    function()
        return test:execsql [[
            SELECT name FROM album JOIN track USING (aid) ORDER BY title DESC, tn DESC
        ]]
    end, {
        -- <3.5a>
        "three-c", "three-a", "two-b", "two-a", "one-c", "one-a"
        -- </3.5a>
    })

test:do_test(
    "3.5b",
    function()
        return test:execsql [[
            SELECT name FROM album JOIN track USING (aid) ORDER BY +title DESC, +tn DESC
        ]]
    end, {
        -- <3.5b>
        "three-c", "three-a", "two-b", "two-a", "one-c", "one-a"
        -- </3.5b>
    })

-- verify same order after sorting
test:do_test(
    "3.5c",
    function()
        return test:execsql [[
            EXPLAIN QUERY PLAN
            SELECT name FROM album JOIN track USING (aid) ORDER BY title DESC, tn DESC
        ]]
    end, {
        -- <3.5c>
        "~/ORDER BY/"
        -- </3.5c>
    })

-- ORDER BY suppressed by uniqueness constraints
test:do_test(
    "3.6a",
    function()
        return test:execsql [[
            SELECT name FROM album CROSS JOIN track USING (aid) ORDER BY title DESC, tn
        ]]
    end, {
        -- <3.6a>
        "three-a", "three-c", "two-a", "two-b", "one-a", "one-c"
        -- </3.6a>
    })

test:do_test(
    "3.6b",
    function()
        return test:execsql [[
            SELECT name FROM album CROSS JOIN track USING (aid)
             ORDER BY +title DESC, +tn
        ]]
    end, {
        -- <3.6b>
        "three-a", "three-c", "two-a", "two-b", "one-a", "one-c"
        -- </3.6b>
    })

-- verify same order after sorting
test:do_test(
    "3.6c",
    function()
        return test:execsql [[
            EXPLAIN QUERY PLAN
            SELECT name FROM album CROSS JOIN track USING (aid) ORDER BY title DESC, tn
        ]]
    end, {
        -- <3.6c>
        "~/ORDER BY/"
        -- </3.6c>
    })

-- inverted ASC/DESC is optimized out
-- # Ticket 5ed1772895bf3deeab78c5e3519b1da9165c541b (2013-06-04)
-- # Incorrect ORDER BY on an indexed JOIN
-- #
test:do_test(
    4.0,
    function()
        return test:execsql [[
            CREATE TABLE t41(a INT PRIMARY KEY, b INT NOT NULL);
            CREATE INDEX t41ba ON t41(b,a);
            CREATE TABLE t42(id INTEGER PRIMARY KEY, x INT NOT NULL REFERENCES t41(a), y INT NOT NULL);
            CREATE UNIQUE INDEX t42xy ON t42(x,y);
            INSERT INTO t41 VALUES(1,1),(3,1);
            INSERT INTO t42 VALUES(1,1,13),(2,1,15),(3,3,14),(4,3,16);

            SELECT b, y FROM t41 CROSS JOIN t42 ON x=a ORDER BY b, y;
        ]]
    end, {
        -- <4.0>
        1, 13, 1, 14, 1, 15, 1, 16
        -- </4.0>
    })

-- # No sorting of queries that omit the FROM clause.
-- #
test:do_execsql_test(
    5.0,
    [[
        EXPLAIN QUERY PLAN SELECT 5 ORDER BY 1
    ]], {
        -- <5.0>

        -- </5.0>
    })

test:do_execsql_test(
    5.1,
    [[
        EXPLAIN QUERY PLAN SELECT 5 UNION ALL SELECT 3 ORDER BY 1
    ]], {
        -- <5.1>
        "~/B-TREE/"
        -- </5.1>
    })

test:do_execsql_test(
    5.2,
    [[
        SELECT 5 UNION ALL SELECT 3 ORDER BY 1
    ]], {
        -- <5.2>
        3, 5
        -- </5.2>
    })

test:do_execsql_test(
    5.3,
    [[
        SELECT 986 AS x GROUP BY X ORDER BY X
    ]], {
        -- <5.3>
        986
        -- </5.3>
    })

-- The following test (originally derived from a single test within fuzz.test)
-- verifies that a PseudoTable cursor is not closed prematurely in a deeply
-- nested query.  This test caused a segfault on 3.8.5 beta.
--
test:do_execsql_test(
    6.0,
    [[
        CREATE TABLE abc(a INT primary key, b INT, c INT);
        INSERT INTO abc VALUES(1, 2, 3);
        INSERT INTO abc VALUES(4, 5, 6);
        INSERT INTO abc VALUES(7, 8, 9);
        SELECT (
          SELECT 'hardware' FROM (
            SELECT 'software' ORDER BY 'firmware' ASC, 'sportswear' DESC
          ) GROUP BY 1 HAVING length(b) <> 0
        )
        FROM abc;
    ]], {
        -- <6.0>
        "hardware", "hardware", "hardware"
        -- </6.0>
    })

-- # Here is a test for a query-planner problem reported on the sql
-- # mailing list on 2014-09-18 by "Merike".  Beginning with version 3.8.0,
-- # a separate sort was being used rather than using the single-column
-- # index.  This was due to an oversight in the indexMightHelpWithOrderby()
-- # routine in where.c.
-- #
-- do_execsql_test 7.0 {
--   CREATE TABLE t7(a INT,b INT);
--   CREATE INDEX t7a ON t7(a);
--   CREATE INDEX t7ab ON t7(a,b);
--   EXPLAIN QUERY PLAN
--   SELECT * FROM t7 WHERE a=?1 ORDER BY rowid;
-- } {~/ORDER BY/}
---------------------------------------------------------------------------
-- Test a partial sort large enough to cause the sorter to spill data
-- to disk.
--
-- reset_db
-- Tarantool: cache_size is not relevant. Comment8.0-2
-- test:do_execsql_test(
--     8.0,
--     [[
--         PRAGMA cache_size = 5;
--         CREATE TABLE t1(id integer primary key, a INT, b INT);
--         CREATE INDEX i1 ON t1(a);
--     ]])

-- -- MUST_WORK_TEST
-- X(518, "X!cmd", [=[["do_eqp_test","8.1","\n  SELECT * FROM t1 ORDER BY a, b;\n","\n  0 0 0 {SCAN TABLE t1 USING INDEX i1} \n  0 0 0 {USE TEMP B-TREE FOR RIGHT PART OF ORDER BY}\n"]]=])
-- test:do_execsql_test(
--     8.2,
--     [[
--         WITH cnt(i) AS (
--           SELECT 1 UNION ALL SELECT i+1 FROM cnt WHERE i<10000
--         )
--         INSERT INTO t1 SELECT i, i%2, randomblob(500) FROM cnt;
--     ]])

-- test:do_test(
--     8.3,
--     function()
--         X(534, "X!cmd", [=[["db","eval"," SELECT * FROM t1 ORDER BY a, b "," incr res $a "]]=])
--         return res
--     end, {
--         -- <8.3>
--         5000
--         -- </8.3>
--     })

test:finish_test()

