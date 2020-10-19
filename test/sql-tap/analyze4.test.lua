#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(4)

--!./tcltestrunner.lua
-- 2011 January 04
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
-- This file implements regression tests for sql library. This file
-- implements tests for ANALYZE to verify that multiple rows containing
-- a NULL value count as distinct rows for the purposes of analyze 
-- statistics.
--
-- Also include test cases for collating sequences on indices.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

test:do_test(
    "analyze4-1.0",
    function()
        test:execsql([[
            CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, a INT ,b TEXT);
            CREATE INDEX t1a ON t1(a);
            CREATE INDEX t1b ON t1(b);
            INSERT INTO t1 (a,b) VALUES(1,NULL);
            INSERT INTO t1 (a,b) SELECT a+1, b FROM t1;
            INSERT INTO t1 (a,b) SELECT a+2, b FROM t1;
            INSERT INTO t1 (a,b) SELECT a+4, b FROM t1;
            INSERT INTO t1 (a,b) SELECT a+8, b FROM t1;
            INSERT INTO t1 (a,b) SELECT a+16, b FROM t1;
            INSERT INTO t1 (a,b) SELECT a+32, b FROM t1;
            INSERT INTO t1 (a,b) SELECT a+64, b FROM t1;
            ANALYZE;
        ]])
        -- Should choose the t1a index since it is more specific than t1b.
        return test:execsql("EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a=5 AND b IS NULL")
    end, {
        -- <analyze4-1.0>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1A (A=?)"
        -- </analyze4-1.0>
    })

-- Verify that the t1b index shows that it does not narrow down the
-- search any at all.
--
test:do_execsql_test(
    "analyze4-1.1",
    [[ SELECT "idx", "stat" FROM "_sql_stat1" WHERE "tbl"='T1' ORDER BY "idx"; ]],
    {
        -- <analyze4-1.1>
        "T1","128 1", "T1A", "128 1", "T1B", "128 128"
        -- </analyze4-1.1>
    })

-- Change half of the b values from NULL to a constant.  Verify
-- that the number of rows selected in stat1 is half the total 
-- number of rows.
--
test:do_test(
    "analyze4-1.2",
    function()
        return test:execsql([[
            UPDATE t1 SET b='x' WHERE a%2;
-- pragma vdbe_debug=1;
            ANALYZE;
-- pragma vdbe_debug=0;
            SELECT "idx", "stat" FROM "_sql_stat1" WHERE "tbl"='T1' ORDER BY "idx";
        ]])
    end, {
        -- <analyze4-1.2>
        "T1", "128 1", "T1A", "128 1", "T1B", "128 64"
        -- </analyze4-1.2>
    })

-- Change the t1.b values all back to NULL.  Add columns t1.c and t1.d.
-- Create a multi-column indices using t1.b and verify that ANALYZE 
-- processes them correctly.
--
test:do_execsql_test(
    "analyze4-1.3",
    [[
            -- Tarantool doesn't suppoort ALTER stmt yet.
            -- UPDATE t1 SET b=NULL;
            --ALTER TABLE t1 ADD COLUMN c;
            --ALTER TABLE t1 ADD COLUMN d;
            -- So, re-create the table and its contents
            DROP TABLE t1;
            CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, a INT ,b INT ,c  INT DEFAULT NULL,d  INT DEFAULT NULL);
            CREATE INDEX t1a ON t1(a);
            CREATE INDEX t1b ON t1(b);
            INSERT INTO t1 (a,b) VALUES(1,NULL);
            INSERT INTO t1 (a,b) SELECT a+1, b FROM t1;
            INSERT INTO t1 (a,b) SELECT a+2, b FROM t1;
            INSERT INTO t1 (a,b) SELECT a+4, b FROM t1;
            INSERT INTO t1 (a,b) SELECT a+8, b FROM t1;
            INSERT INTO t1 (a,b) SELECT a+16, b FROM t1;
            INSERT INTO t1 (a,b) SELECT a+32, b FROM t1;
            INSERT INTO t1 (a,b) SELECT a+64, b FROM t1;

            UPDATE t1 SET c=a/4, d=a/2;
            CREATE INDEX t1bcd ON t1(b,c,d);
            CREATE INDEX t1cdb ON t1(c,d,b);
            CREATE INDEX t1cbd ON t1(c,b,d);
            ANALYZE;
            SELECT "idx", "stat" FROM "_sql_stat1" WHERE "tbl"='T1' ORDER BY "idx";
    ]]
    , {
        -- <analyze4-1.3>
        "T1","128 1", "T1A", "128 1", "T1B", "128 128", "T1BCD", "128 128 4 2", "T1CBD", "128 4 4 2", "T1CDB", "128 4 2 2"
        -- </analyze4-1.3>
    })

-- Verify that collating sequences are taken into account when computing
-- ANALYZE statistics.
--
-- Tarantool: commented due to #2121
-- test:do_test(
--     "analyze4-2.0",
--     function()
--         return test:execsql([[
--             CREATE TABLE t2(
--               x INTEGER PRIMARY KEY,
--               a TEXT COLLATE nocase,
--               b TEXT COLLATE rtrim,
--               c TEXT COLLATE binary
--             );
--             CREATE INDEX t2a ON t2(a);
--             CREATE INDEX t2b ON t2(b);
--             CREATE INDEX t2c ON t2(c);
--             CREATE INDEX t2c2 ON t2(c COLLATE nocase);
--             CREATE INDEX t2c3 ON t2(c COLLATE rtrim);
--             INSERT INTO t2 VALUES(1, 'abc', 'abc', 'abc');
--             INSERT INTO t2 VALUES(2, 'abC', 'abC', 'abC');
--             INSERT INTO t2 VALUES(3, 'abc ', 'abc ', 'abc ');
--             INSERT INTO t2 VALUES(4, 'abC ', 'abC ', 'abC ');
--             INSERT INTO t2 VALUES(5, 'aBc', 'aBc', 'aBc');
--             INSERT INTO t2 VALUES(6, 'aBC', 'aBC', 'aBC');
--             INSERT INTO t2 VALUES(7, 'aBc ', 'aBc ', 'aBc ');
--             INSERT INTO t2 VALUES(8, 'aBC ', 'aBC ', 'aBC ');
--             ANALYZE;
--             SELECT idx, stat FROM _sql_stat1 WHERE tbl='t2' ORDER BY idx;
--         ]])
--     end, {
--         -- <analyze4-2.0>
--         "t2a", "8 4", "t2b", "8 2", "t2c", "8 1", "t2c2", "8 4", "t2c3", "8 2"
--         -- </analyze4-2.0>
--     })

test:finish_test()
