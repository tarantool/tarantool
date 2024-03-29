#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(10)

--!./tcltestrunner.lua
-- 2015-03-16
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
-- focus of this file is testing OR expressions where terms can be
-- factored from either side of the OR and combined into a single new
-- AND term that is beneficial to the search.  Examples:
--
--      (x>A OR x=A)              -->    ... AND (x>=A)
--      (x>A OR (x=A AND y>=B)    -->    ... AND (x>=A)
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(1.1, [[
  CREATE TABLE t1(a INT ,b INT ,c INT , primary key (a,b,c));
  WITH RECURSIVE c(x) AS (VALUES(0) UNION ALL SELECT x+1 FROM c WHERE x<99)
    INSERT INTO t1(a,b,c) SELECT x, x/10, x%10 FROM c;
  CREATE INDEX t1bc ON t1(b,c);
  SELECT a FROM t1 WHERE b>9 OR b=9 ORDER BY +a;
]], {
  -- <1.1>
  90, 91, 92, 93, 94, 95, 96, 97, 98, 99
  -- </1.1>
})

test:do_execsql_test("1.1eqp", [[
  EXPLAIN QUERY PLAN
  SELECT a FROM t1 WHERE b>9 OR b=9 ORDER BY +a;
]], {
  -- <1.1eqp>
  "/SEARCH TABLE t1 USING COVERING INDEX t1bc/"
  -- </1.1eqp>
})

test:do_execsql_test(1.2, [[
  SELECT a FROM t1 WHERE b>8 OR (b=8 AND c>7) ORDER BY +a;
]], {
  -- <1.2>
  88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99
  -- </1.2>
})

test:do_execsql_test("1.2eqp", [[
  EXPLAIN QUERY PLAN
  SELECT a FROM t1 WHERE b>8 OR (b=8 AND c>7) ORDER BY +a;
]], {
  -- <1.2eqp>
  "/SEARCH TABLE t1 USING COVERING INDEX t1bc/"
  -- </1.2eqp>
})

test:do_execsql_test(1.3, [[
  SELECT a FROM t1 WHERE (b=8 AND c>7) OR b>8 ORDER BY +a;
]], {
  -- <1.3>
  88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99
  -- </1.3>
})

test:do_execsql_test("1.3eqp", [[
  EXPLAIN QUERY PLAN
  SELECT a FROM t1 WHERE (b=8 AND c>7) OR b>8 ORDER BY +a;
]], {
  -- <1.3eqp>
  "/SEARCH TABLE t1 USING COVERING INDEX t1bc/"
  -- </1.3eqp>
})

test:do_execsql_test(1.4, [[
  SELECT a FROM t1 WHERE (b=8 AND c>7) OR 8<b ORDER BY +a;
]], {
  -- <1.4>
  88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99
  -- </1.4>
})

test:do_execsql_test("1.4eqp", [[
  EXPLAIN QUERY PLAN
  SELECT a FROM t1 WHERE (b=8 AND c>7) OR 8<b ORDER BY +a;
]], {
  -- <1.4eqp>
  "/SEARCH TABLE t1 USING COVERING INDEX t1bc/"
  -- </1.4eqp>
})

test:do_execsql_test(1.5, [[
  SELECT a FROM t1 WHERE (b=8 AND c>7) OR (b>8 AND c NOT IN (4,5,6))
   ORDER BY +a;
]], {
  -- <1.5>
  88, 89, 90, 91, 92, 93, 97, 98, 99
  -- </1.5>
})

test:do_execsql_test("1.5eqp", [[
  EXPLAIN QUERY PLAN
  SELECT a FROM t1 WHERE (b=8 AND c>7) OR (b>8 AND c NOT IN (4,5,6))
   ORDER BY +a;
]], {
  -- <1.5eqp>
  "/SEARCH TABLE t1 USING COVERING INDEX t1bc/"
  -- </1.5eqp>
})

test:finish_test()

