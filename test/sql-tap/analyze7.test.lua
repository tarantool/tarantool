#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(14)

--!./tcltestrunner.lua
-- 2011 April 1
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements tests for the ANALYZE command when an idnex
-- name is given as the argument.
--

test:do_test(
	"analyze7-1.0",
	function()
		return test:execsql([[
		    DROP TABLE IF EXISTS t1;
			CREATE TABLE t1(id PRIMARY KEY, a, b, c, d);
			CREATE INDEX t1a ON t1(a);
			CREATE INDEX t1b ON t1(b);
			CREATE INDEX t1cd ON t1(c, d);
		    DROP TABLE IF EXISTS nums;
			CREATE TABLE nums(n PRIMARY KEY);
			INSERT into nums WITH RECURSIVE cnt(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM cnt WHERE x<256) SELECT x FROM cnt;
 			INSERT INTO t1 SELECT n, n, n, n/100, n FROM nums;
 			EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a=123;
		]])
		end, {
        	-- <analyze7-1.0>
            0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1A (A=?)"
            -- </analyze7-1.0>
        })

test:do_execsql_test(
	"analyze7-1.1",
	[[
		EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE b=123;
	]], {
    	-- <analyze7-1.1>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1B (B=?)"
        -- </analyze7-1.1>
    })

test:do_execsql_test(
	"analyze7-1.2",
	[[
		EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE c=2;
	]], {
        -- <analyze7-1.2>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1CD (C=?)"
        -- </analyze7-1.2>
    })

-- Run an analyze on one of the three indices.  Verify that this
-- effects the row-count estimate on the one query that uses that
-- one index.
--
test:do_test(
	"analyze7-2.0",
	function()
		test:execsql("ANALYZE;")
		return test:execsql("EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a=123;")
	end, {
       	-- <analyze7-2.0>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1A (A=?)"
        -- </analyze7-2.0>
    })

test:do_execsql_test(
	"analyze7-2.1",
	[[
		EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE b=123;
	]], {
        -- <analyze7-2.1>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1B (B=?)"
        -- </analyze7-2.1>
    })

test:do_execsql_test(
	"analyze7-2.2",
	[[
		EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE c=2;
	]], {
        -- <analyze7-2.2>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1CD (C=?)"
        -- </analyze7-2.2>
    })

-- Verify that since the query planner now things that t1a is more
-- selective than t1b, it prefers to use t1a.
-- Tarantool: can't run analyze on certain index, so just run global analyze and drop index.
test:do_execsql_test(
	"analyze7-2.3",
	[[
		DROP INDEX t1b ON t1;
		CREATE INDEX t1b ON t1(b);
		EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a=123 AND b=123;
	]], {
        -- <analyze7-2.3>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1A (A=?)"
        -- </analyze7-2.3>
    })

-- Run an analysis on another of the three indices.  Verify  that this
-- new analysis works and does not disrupt the previous analysis.
--
test:do_test(
	"analyze7-3.0",
	function()
		test:execsql("ANALYZE;")
		return test:execsql("EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE a=123;")
	end, {
        -- <analyze7-3.0>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1A (A=?)"
        -- </analyze7-3.0>
    })

test:do_execsql_test(
	"analyze7-3.1",
	[[
		EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE b=123;
	]], {
        -- <analyze7-3.1>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1B (B=?)"
        -- </analyze7-3.1>
    })

test:do_execsql_test(
	"analyze7-3.2.1",
	[[
		EXPLAIN QUERY PLAN SELECT * FROM T1 WHERE C=?;
	]], {
        -- <analyze7-3.2.1>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1CD (C=?)"
        -- </analyze7-3.2.1>
    })

-- SQLite comes up with a different estimated
-- row count for (c=2) than it does for (c=?).
test:do_execsql_test(
    "analyze7-3.2.2",
    [[
    	EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE c=2;
    ]], {
    	-- <analyze7-3.2.2>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1CD (C=?)"
        -- </analyze7-3.2.2>
    })

test:do_execsql_test(
	"analyze7-3.3",
	[[
		EXPLAIN QUERY PLAN SELECT * FROM T1 WHERE A=123 AND B=123;
	]], {
		-- After running second ANALYZE query, there are equal statistics for
		-- indexes t1a and t1b, so it doesn't really matter which index planner uses.
        -- <analyze7-3.3>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1A (A=?)"
        --0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1B (B=?)"
        -- </analyze7-3.3>
    })

test:do_execsql_test(
	"analyze7-3.4",
	[[
		EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE c=123 AND b=123;
	]], {
        -- <analyze7-3.4>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1CD (C=?)"
        -- </analyze7-3.4>
    })

test:do_execsql_test(
	"analyze7-3.6",
	[[
		EXPLAIN QUERY PLAN SELECT * FROM t1 WHERE c=123 AND d=123 AND b=123;
	]], {
       -- <analyze7-3.6>
       0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1CD (C=? AND D=?)"
       -- </analyze7-3.6>
    })

test:finish_test()
