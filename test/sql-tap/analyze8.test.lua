#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(15)

--!./tcltestrunner.lua
-- 2011 August 13
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
-- This file implements tests for SQLite library.  The focus of the tests
-- in this file is testing the capabilities of sqlite_stat3.
--

testprefix = "analyze8"

-- Scenario:
--
--    Two indices.  One has mostly singleton entries, but for a few
--    values there are hundreds of entries.  The other has 10-20
--    entries per value.
--
-- Verify that the query planner chooses the first index for the singleton
-- entries and the second index for the others.
--
test:do_test(
    1.0,
    function()
        test:execsql([[
            CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, a INT ,b INT ,c INT ,d INT );
            CREATE INDEX t1a ON t1(a);
            CREATE INDEX t1b ON t1(b);
            CREATE INDEX t1c ON t1(c);
        ]])
        for i=0, 1000 do
            if (i % 2 == 0)
            then
                a = i
            else
                a =(i % 8) * 100
            end
            b = math.floor(i / 10)
            c = math.floor(i / 8)
            c = ((c * c) * c)
            test:execsql("INSERT INTO t1 (a,b,c,d) VALUES("..a..", "..b..", "..c..", "..i..")")
        end
        return test:execsql("ANALYZE")
    end, {
        -- <1.0>        
        -- </1.0>
    })

-- The a==100 comparison is expensive because there are many rows
-- with a==100.  And so for those cases, choose the t1b index.

-- But for a==99 and a==101, there are far fewer rows so choose
-- the t1a index.
    
test:do_eqp_test(
    1.1,
    [[SELECT * FROM t1 WHERE a=100 AND b=55]],
    {
        -- <1.1>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1B (B=?)"}
        -- </1.1>
    })

test:do_eqp_test(
    1.2,
    [[SELECT * FROM t1 WHERE a=99 AND b=55]],
    {
        -- <1.2>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1A (A=?)"}
        -- </1.2>
    })

test:do_eqp_test(
    1.3,
    [[SELECT * FROM t1 WHERE a=101 AND b=55]],
    {
        -- <1.3>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1A (A=?)"}
        -- </1.3>
    })

test:do_eqp_test(
    1.4,
    [[SELECT * FROM t1 WHERE a=100 AND b=56]],
    {
        -- <1.4>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1B (B=?)"}
        -- </1.4>
    })

test:do_eqp_test(
    1.5,
    [[SELECT * FROM t1 WHERE a=99 AND b=56]],
    {
        -- <1.5>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1A (A=?)"}
        -- </1.5>
    })

test:do_eqp_test(
    1.6,
    [[SELECT * FROM t1 WHERE a=101 AND b=56]],
    {
        -- <1.6>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1A (A=?)"}
        -- </1.6>
    })

test:do_eqp_test(
    2.1,
    [[SELECT * FROM t1 WHERE a=100 AND b BETWEEN 50 AND 54]],
    {
        -- <2.1>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1B (B>? AND B<?)"}
        -- </2.1>
    })

-- There are many more values of c between 0 and 100000 than there are
-- between 800000 and 900000.  So t1c is more selective for the latter
-- range.
-- 
-- Test 3.2 is a little unstable. It depends on the planner estimating
-- that (b BETWEEN 30 AND 34) will match more rows than (c BETWEEN
-- 800000 AND 900000). Which is a pretty close call (50 vs. 32), so
-- the planner could get it wrong with an unlucky set of samples. This
-- case happens to work, but others ("b BETWEEN 40 AND 44" for example) 
-- will fail.
--
test:do_execsql_test(
    "3.0.0",
    [[
        SELECT count(*) FROM t1 WHERE b BETWEEN 30 AND 34
    ]], {
        -- <3.0>
        50
        -- </3.0>
    })

test:do_execsql_test(
    "3.0.1",
    [[
        SELECT count(*) FROM t1 WHERE c BETWEEN 0 AND 100000;
    ]], {
        -- <3.0>
        376
        -- </3.0>
    })

test:do_execsql_test(
    "3.0.2",
    [[
        SELECT count(*) FROM t1 WHERE c BETWEEN 800000 AND 900000;
    ]], {
        -- <3.0>
        32
        -- </3.0>
    })

test:do_eqp_test(
    "3.1",
    [[SELECT * FROM t1 WHERE b BETWEEN 30 AND 34 AND c BETWEEN 0 AND 100000]],
    {
        -- <3.1>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1B (B>? AND B<?)"}
        -- </3.1>
    })

test:do_eqp_test(
    "3.2",
    [[SELECT * FROM t1
       WHERE b BETWEEN 30 AND 34 AND c BETWEEN 800000 AND 900000]],
    {
        -- <3.2>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1C (C>? AND C<?)"}
        -- {0, 0, 0, "SEARCH TABLE t1 USING COVERING INDEX t1b (b>? AND b<?)"}
    })

test:do_eqp_test(
    3.3,
    [[SELECT * FROM t1 WHERE a=100 AND c BETWEEN 0 AND 100000]],
    {
        -- <3.3>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1A (A=?)"}
        -- </3.3>
    })

test:do_eqp_test(
    "3.4",
    [[SELECT * FROM t1
       WHERE a=100 AND c BETWEEN 800000 AND 900000]],
    {
        -- <3.4>
        {0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1C (C>? AND C<?)"}
        -- </3.4>
    })

test:finish_test()
