#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(48)

testprefix = "analyzeE"

--!./tcltestrunner.lua
-- 2014-10-08
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements tests for using STAT4 information
-- on a descending index in a range query.
--
-- Verify that range queries on an ASCENDING index will use the
-- index only if the range covers only a small fraction of the
-- entries.

test:do_execsql_test(
    "analyzeE-1.0",
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(id INTEGER PRIMARY KEY, a INT , b INT );
        WITH RECURSIVE cnt(x) AS (VALUES(1000) UNION ALL SELECT x+1 FROM cnt WHERE x<2000) INSERT INTO t1(id, a, b) SELECT x, x, x FROM cnt;
        CREATE INDEX t1a ON t1(a);
        CREATE INDEX t1b ON t1(b);
        ANALYZE;
    ]], {
        -- <analyzeE-1.0>
        -- </analyzeE-1.0>
    })


test:do_execsql_test(
   "analyzeE-1.1",
   [[
       EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 500 AND 2500;
   ]], {
       -- <analyzeE-1.1>
       0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A>? AND A<?)'
       -- </analyzeE-1.1>
   })

test:do_execsql_test(
    "analyzeE-1.2",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE b BETWEEN 2900 AND 3000;
    ]], {
        -- <analyzeE-1.2>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1B (B>? AND B<?)'
        -- </analyzeE-1.2>
    })

test:do_execsql_test(
    "analyzeE-1.3",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 1700 AND 1750;
    ]], {
        -- <analyzeE-1.3>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A>? AND A<?)'
        -- </analyzeE-1.3>
    })

test:do_execsql_test(
    "analyzeE-1.4",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 1 AND 500;
    ]], {
        -- <analyzeE-1.4>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A>? AND A<?)'
        -- </analyzeE-1.4>
    })

test:do_execsql_test(
    "analyzeE-1.5",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 3000 AND 3000000;
    ]], {
        -- <analyzeE-1.5>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A>? AND A<?)'
        -- </analyzeE-1.5>
    })

test:do_execsql_test(
    "analyzeE-1.6",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a<500;
    ]], {
        -- <analyzeE-1.6>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A<?)'
        -- </analyzeE-1.6>
    })

test:do_execsql_test(
    "analyzeE-1.7",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a>2500;
    ]], {
        -- <analyzeE-1.7>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A>?)'
        -- </analyzeE-1.7>
    })

test:do_execsql_test(
    "analyzeE-1.8",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a>1900;
    ]], {
        -- <analyzeE-1.8>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A>?)'
        -- </analyzeE-1.8>
    })

test:do_execsql_test(
   "analyzeE-1.9",
   [[
       EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a>1100;
   ]], {
       -- <analyzeE-1.9>
       0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A>?)'
       -- </analyzeE-1.9>
   })

test:do_execsql_test(
    "analyzeE-1.10",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a<1100;
    ]], {
        -- <analyzeE-1.10>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A<?)'
        -- </analyzeE-1.10>
    })

test:do_execsql_test(
   "analyzeE-1.11",
   [[
       EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a<1900;
   ]], {
        -- <analyzeE-1.11>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A<?)'
        -- </analyzeE-1.11>
   })

-- Verify that everything works the same on a DESCENDING index.
--
test:do_execsql_test(
    "analyzeE-2.0",
    [[
        DROP INDEX t1a on t1;
        CREATE INDEX t1a ON t1(a DESC);
        ANALYZE;
    ]], {
        -- <analyzeE-2.0>
        -- </analyzeE-2.0>
    })

test:do_execsql_test(
    "analyzeE-2.1",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 500 AND 2500;
    ]], {
        -- <analyzeE-2.1>
        0,0,0,"SEARCH TABLE T1 USING COVERING INDEX T1A (A>? AND A<?)"
        -- </analyzeE-2.1>
    })

test:do_execsql_test(
    "analyzeE-2.2",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 2900 AND 3000;
    ]], {
        -- <analyzeE-2.2>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A>? AND A<?)'
        -- </analyzeE-2.2>
    })

test:do_execsql_test(
    "analyzeE-2.3",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 1700 AND 1750;
    ]], {
        -- <analyzeE-2.3>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A>? AND A<?)'
        -- </analyzeE-2.3>
    })

test:do_execsql_test(
    "analyzeE-2.4",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 1 AND 500;
    ]], {
        -- <analyzeE-2.4>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A>? AND A<?)'
        -- </analyzeE-2.4>
    })

test:do_execsql_test(
    "analyzeE-2.5",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 3000 AND 3000000;
    ]], {
        -- <analyzeE-2.5>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A>? AND A<?)'
        -- </analyzeE-2.5>
    })

test:do_execsql_test(
    "analyzeE-2.6",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a<500;
    ]], {
        -- <analyzeE-2.6>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A<?)'
        -- </analyzeE-2.6>
    })

test:do_execsql_test(
    "analyzeE-2.7",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a>2500;
    ]], {
        -- <analyzeE-2.7>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A>?)'
        -- </analyzeE-2.7>
    })

test:do_execsql_test(
    "analyzeE-2.8",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a>1900;
    ]], {
        -- <analyzeE-2.8>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A>?)'
        -- </analyzeE-2.8>
    })

test:do_execsql_test(
    "analyzeE-2.9",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a>1100;
    ]], {
        -- <analyzeE-2.9>
        0,0,0,"SEARCH TABLE T1 USING COVERING INDEX T1A (A>?)"
        -- </analyzeE-2.9>
    })

test:do_execsql_test(
    "analyzeE-2.10",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a<1100;
    ]], {
        -- <analyzeE-2.10>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1A (A<?)'
        -- </analyzeE-2.10>
    })

test:do_execsql_test(
    "analyzeE-2.11",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a<1900;
    ]], {
        -- <analyzeE-2.11>
        0,0,0,"SEARCH TABLE T1 USING COVERING INDEX T1A (A<?)"
        -- </analyzeE-2.11>
    })

-- Now do a range query on the second term of an ASCENDING index
-- where the first term is constrained by equality.
--
test:do_execsql_test(
    "analyzeE-3.0",
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(id  INT PRIMARY KEY,a INT ,b INT ,c INT );
        WITH RECURSIVE cnt(x) AS (VALUES(1000) UNION ALL SELECT x+1 FROM cnt WHERE x<2000) INSERT INTO t1(id,a,b,c) SELECT x, x, x, 123 FROM cnt;
        CREATE INDEX t1ca ON t1(c,a);
        ANALYZE;
    ]], {
        -- <analyzeE-3.0>
        
        -- </analyzeE-3.0>
    })

test:do_execsql_test(
    "analyzeE-3.1",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 500 AND 2500 AND c=123;
    ]], {
        -- <analyzeE-3.1>
        0,0,0,"SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>? AND A<?)"
        -- </analyzeE-3.1>
    })

test:do_execsql_test(
    "analyzeE-3.2",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 2900 AND 3000 AND c=123;
    ]], {
        -- <analyzeE-3.2>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>? AND A<?)'
        -- </analyzeE-3.2>
    })

test:do_execsql_test(
    "analyzeE-3.3",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 1700 AND 1750 AND c=123;
    ]], {
        -- <analyzeE-3.3>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>? AND A<?)'
        -- </analyzeE-3.3>
    })

test:do_execsql_test(
    "analyzeE-3.4",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 1 AND 500 AND c=123;
    ]], {
        -- <analyzeE-3.4>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>? AND A<?)'
        -- </analyzeE-3.4>
    })

test:do_execsql_test(
    "analyzeE-3.5",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 3000 AND 3000000 AND c=123;
    ]], {
        -- <analyzeE-3.5>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>? AND A<?)'
        -- </analyzeE-3.5>
    })

test:do_execsql_test(
    "analyzeE-3.6",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a<500 AND c=123;
    ]], {
        -- <analyzeE-3.6>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A<?)'
        -- </analyzeE-3.6>
    })

test:do_execsql_test(
    "analyzeE-3.7",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a>2500 AND c=123;
    ]], {
        -- <analyzeE-3.7>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>?)'
        -- </analyzeE-3.7>
    })

test:do_execsql_test(
    "analyzeE-3.8",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a>1900 AND c=123;
    ]], {
        -- <analyzeE-3.8>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>?)'
        -- </analyzeE-3.8>
    })

test:do_execsql_test(
    "analyzeE-3.9",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a>1100 AND c=123;
    ]], {
        -- <analyzeE-3.9>
        0, 0, 0,"SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>?)"
        -- </analyzeE-3.9>
    })

test:do_execsql_test(
    "analyzeE-3.10",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a<1100 AND c=123;
    ]], {
        -- <analyzeE-3.10>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A<?)'
        -- </analyzeE-3.10>
    })

test:do_execsql_test(
    "analyzeE-3.11",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a<1900 AND c=123;
    ]], {
        -- <analyzeE-3.11>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A<?)"
        -- </analyzeE-3.11>
    })

-- Repeat the 3.x tests using a DESCENDING index
--
test:do_execsql_test(
    "analyzeE-4.0",
    [[
        DROP INDEX t1ca ON t1;
        CREATE INDEX t1ca ON t1(c ASC,a DESC);
        ANALYZE;
    ]], {
        -- <analyzeE-4.0>
        
        -- </analyzeE-4.0>
    })

test:do_execsql_test(
    "analyzeE-4.1",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 500 AND 2500 AND c=123;
    ]], {
        -- <analyzeE-4.1>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>? AND A<?)"
        -- </analyzeE-4.1>
    })

test:do_execsql_test(
    "analyzeE-4.2",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 2900 AND 3000 AND c=123;
    ]], {
        -- <analyzeE-4.2>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>? AND A<?)'
        -- </analyzeE-4.2>
    })

test:do_execsql_test(
    "analyzeE-4.3",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 1700 AND 1750 AND c=123;
    ]], {
        -- <analyzeE-4.3>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>? AND A<?)'
        -- </analyzeE-4.3>
    })

test:do_execsql_test(
    "analyzeE-4.4",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 1 AND 500 AND c=123;
    ]], {
        -- <analyzeE-4.4>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>? AND A<?)'
        -- </analyzeE-4.4>
    })

test:do_execsql_test(
    "analyzeE-4.5",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a BETWEEN 3000 AND 3000000 AND c=123;
    ]], {
        -- <analyzeE-4.5>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>? AND A<?)'
        -- </analyzeE-4.5>
    })

test:do_execsql_test(
    "analyzeE-4.6",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a<500 AND c=123;
    ]], {
        -- <analyzeE-4.6>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A<?)'
        -- </analyzeE-4.6>
    })

test:do_execsql_test(
    "analyzeE-4.7",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a>2500 AND c=123;
    ]], {
        -- <analyzeE-4.7>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>?)'
        -- </analyzeE-4.7>
    })

test:do_execsql_test(
    "analyzeE-4.8",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a>1900 AND c=123;
    ]], {
        -- <analyzeE-4.8>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>?)'
        -- </analyzeE-4.8>
    })

test:do_execsql_test(
    "analyzeE-4.9",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a>1100 AND c=123;
    ]], {
        -- <analyzeE-4.9>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A>?)"
        -- </analyzeE-4.9>
    })

test:do_execsql_test(
    "analyzeE-4.10",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a<1100 AND c=123;
    ]], {
        -- <analyzeE-4.10>
        0, 0, 0, 'SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A<?)'
        -- </analyzeE-4.10>
    })

test:do_execsql_test(
    "analyzeE-4.11",
    [[
        EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a<1900 AND c=123;
    ]], {
        -- <analyzeE-4.11>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1CA (C=? AND A<?)"
        -- </analyzeE-4.11>
    })


test:finish_test()
