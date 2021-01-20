#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(38)

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
-- $Id: select3.test,v 1.23 2008/01/16 18:20:42 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Build some test data
--
test:do_test("select3-1.0", function()
  test:execsql [[
    DROP TABLE IF EXISTS t1;
    CREATE TABLE t1(n int primary key, log int);
    START TRANSACTION;
  ]]
  for i = 1, 32-1 do -- in X(0, "X!for", [=[["set i 1","$i<32","incr i"]]=]) do
     local j = 0
     while bit.lshift(1, j) < i do
	-- for _ in X(0, "X!for", [=[["set j 0","(1<<$j)<$i","incr j"]]=]) do
	j = j + 1
     end
    test:execsql(string.format("INSERT INTO t1 VALUES(%s,%s)", i, j))
  end
  test:execsql [[
    COMMIT
  ]]
  return test:execsql "SELECT DISTINCT log FROM t1 ORDER BY log"
end, {
  -- <select3-1.0>
  0, 1, 2, 3, 4, 5
  -- </select3-1.0>
})

-- Basic aggregate functions.
--
test:do_execsql_test("select3-1.1", [[
  SELECT count(*) FROM t1
]], {
  -- <select3-1.1>
  31
  -- </select3-1.1>
})

test:do_execsql_test("select3-1.2", [[
  SELECT min(n),min(log),max(n),max(log),sum(n),sum(log),avg(n),avg(log)
  FROM t1
]], {
  -- <select3-1.2>
  1, 0, 31, 5, 496, 124, 16.0, 4.0
  -- </select3-1.2>
})

test:do_execsql_test("select3-1.3", [[
  SELECT max(n)/avg(n), max(log)/avg(log) FROM t1
]], {
  -- <select3-1.3>
  1.9375, 1.25
  -- </select3-1.3>
})

-- Try some basic GROUP BY clauses
--
test:do_execsql_test("select3-2.1", [[
  SELECT log, count(*) FROM t1 GROUP BY log ORDER BY log
]], {
  -- <select3-2.1>
  0, 1, 1, 1, 2, 2, 3, 4, 4, 8, 5, 15
  -- </select3-2.1>
})

test:do_execsql_test("select3-2.2", [[
  SELECT log, min(n) FROM t1 GROUP BY log ORDER BY log
]], {
  -- <select3-2.2>
  0, 1, 1, 2, 2, 3, 3, 5, 4, 9, 5, 17
  -- </select3-2.2>
})

test:do_execsql_test("select3-2.3.1", [[
  SELECT log, avg(n) FROM t1 GROUP BY log ORDER BY log
]], {
  -- <select3-2.3.1>
  0, 1.0, 1, 2.0, 2, 3.5, 3, 6.5, 4, 12.5, 5, 24.0
  -- </select3-2.3.1>
})

test:do_execsql_test("select3-2.3.2", [[
  SELECT log, avg(n)+1 FROM t1 GROUP BY log ORDER BY log
]], {
  -- <select3-2.3.2>
  0, 2.0, 1, 3.0, 2, 4.5, 3, 7.5, 4, 13.5, 5, 25.0
  -- </select3-2.3.2>
})

test:do_execsql_test("select3-2.4", [[
  SELECT log, avg(n)-min(n) FROM t1 GROUP BY log ORDER BY log
]], {
  -- <select3-2.4>
  0, 0.0, 1, 0.0, 2, 0.5, 3, 1.5, 4, 3.5, 5, 7.0
  -- </select3-2.4>
})

test:do_execsql_test("select3-2.5", [[
  SELECT log*2+1, avg(n)-min(n) FROM t1 GROUP BY log ORDER BY log
]], {
  -- <select3-2.5>
  1, 0.0, 3, 0.0, 5, 0.5, 7, 1.5, 9, 3.5, 11, 7.0
  -- </select3-2.5>
})

test:do_execsql_test("select3-2.6", [[
  SELECT log*2+1 as x, count(*) FROM t1 GROUP BY x ORDER BY x
]], {
  -- <select3-2.6>
  1, 1, 3, 1, 5, 2, 7, 4, 9, 8, 11, 15
  -- </select3-2.6>
})

test:do_execsql_test("select3-2.7", [[
  SELECT log*2+1 AS x, count(*) AS y FROM t1 GROUP BY x ORDER BY y, x
]], {
  -- <select3-2.7>
  1, 1, 3, 1, 5, 2, 7, 4, 9, 8, 11, 15
  -- </select3-2.7>
})

test:do_execsql_test("select3-2.8", [[
  SELECT log*2+1 AS x, count(*) AS y FROM t1 GROUP BY x ORDER BY 10-(x+y)
]], {
  -- <select3-2.8>
  11, 15, 9, 8, 7, 4, 5, 2, 3, 1, 1, 1
  -- </select3-2.8>
})

-- MUST_WORK_TEST
--do_test select3-2.9 {
--  catchsql {
--    SELECT log, count(*) FROM t1 GROUP BY 'x' ORDER BY log;
--  }
--} {1 {GROUP BY terms must not be non-integer constants}}
test:do_catchsql_test("select3-2.10", [[
  SELECT log, count(*) FROM t1 GROUP BY 0 ORDER BY log;
]], {
  -- <select3-2.10>
  1, "Error at GROUP BY in place 1: term out of range - should be between 1 and 2"
  -- </select3-2.10>
})

test:do_catchsql_test("select3-2.11", [[
  SELECT log, count(*) FROM t1 GROUP BY 3 ORDER BY log;
]], {
  -- <select3-2.11>
  1, "Error at GROUP BY in place 1: term out of range - should be between 1 and 2"
  -- </select3-2.11>
})

test:do_catchsql_test("select3-2.12", [[
  SELECT log, count(*) FROM t1 GROUP BY 1 ORDER BY log;
]], {
  -- <select3-2.12>
  0, {0, 1, 1, 1, 2, 2, 3, 4, 4, 8, 5, 15}
  -- </select3-2.12>
})

-- Cannot have an empty GROUP BY
test:do_catchsql_test("select3-2.13", [[
  SELECT log, count(*) FROM t1 GROUP BY ORDER BY log;
]], {
  -- <select3-2.13>
  1, [[At line 1 at or near position 41: keyword 'ORDER' is reserved. Please use double quotes if 'ORDER' is an identifier.]]
  -- </select3-2.13>
})

test:do_catchsql_test("select3-2.14", [[
  SELECT log, count(*) FROM t1 GROUP BY;
]], {
  -- <select3-2.14>
  1, [[Syntax error at line 1 near ';']]
  -- </select3-2.14>
})

-- Cannot have a HAVING without a GROUP BY
--
test:do_catchsql_test("select3-3.1", [[
  SELECT log, count(*) FROM t1 HAVING log>=4
]], {
  -- <select3-3.1>
  1, "Failed to execute SQL statement: HAVING argument must appear in the GROUP BY clause or be used in an aggregate function"
  -- </select3-3.1>
})

-- Toss in some HAVING clauses
--
test:do_execsql_test("select3-4.1", [[
  SELECT log, count(*) FROM t1 GROUP BY log HAVING log>=4 ORDER BY log
]], {
  -- <select3-4.1>
  4, 8, 5, 15
  -- </select3-4.1>
})

test:do_execsql_test("select3-4.2", [[
  SELECT log, count(*) FROM t1
  GROUP BY log
  HAVING count(*)>=4
  ORDER BY log
]], {
  -- <select3-4.2>
  3, 4, 4, 8, 5, 15
  -- </select3-4.2>
})

test:do_execsql_test("select3-4.3", [[
  SELECT log, count(*) FROM t1
  GROUP BY log
  HAVING count(*)>=4
  ORDER BY max(n)+0
]], {
  -- <select3-4.3>
  3, 4, 4, 8, 5, 15
  -- </select3-4.3>
})

test:do_execsql_test("select3-4.4", [[
  SELECT log AS x, count(*) AS y FROM t1
  GROUP BY x
  HAVING y>=4
  ORDER BY max(n)+0
]], {
  -- <select3-4.4>
  3, 4, 4, 8, 5, 15
  -- </select3-4.4>
})

test:do_execsql_test("select3-4.5", [[
  SELECT log AS x FROM t1
  GROUP BY x
  HAVING count(*)>=4
  ORDER BY max(n)+0
]], {
  -- <select3-4.5>
  3, 4, 5
  -- </select3-4.5>
})

test:do_execsql_test("select3-5.1", [[
  SELECT log, count(*), avg(n), max(n+log*2) FROM t1
  GROUP BY log
  ORDER BY max(n+log*2)+0, avg(n)+0
]], {
  -- <select3-5.1>
  0, 1, 1.0, 1, 1, 1, 2.0, 4, 2, 2, 3.5, 8, 3, 4, 6.5, 14, 4, 8, 12.5, 24, 5, 15, 24.0, 41
  -- </select3-5.1>
})

test:do_execsql_test("select3-5.2", [[
  SELECT log, count(*), avg(n), max(n+log*2) FROM t1
  GROUP BY log
  ORDER BY max(n+log*2)+0, GREATEST(log,avg(n))+0
]], {
  -- <select3-5.2>
  0, 1, 1.0, 1, 1, 1, 2.0, 4, 2, 2, 3.5, 8, 3, 4, 6.5, 14, 4, 8, 12.5, 24, 5, 15, 24.0, 41
  -- </select3-5.2>
})

-- Test sorting of GROUP BY results in the presence of an index
-- on the GROUP BY column.
--
test:do_execsql_test("select3-6.1", [[
  SELECT log, min(n) FROM t1 GROUP BY log ORDER BY log;
]], {
  -- <select3-6.1>
  0, 1, 1, 2, 2, 3, 3, 5, 4, 9, 5, 17
  -- </select3-6.1>
})

test:do_execsql_test("select3-6.2", [[
  SELECT log, min(n) FROM t1 GROUP BY log ORDER BY log DESC;
]], {
  -- <select3-6.2>
  5, 17, 4, 9, 3, 5, 2, 3, 1, 2, 0, 1
  -- </select3-6.2>
})

test:do_execsql_test("select3-6.3", [[
  SELECT log, min(n) FROM t1 GROUP BY log ORDER BY 1;
]], {
  -- <select3-6.3>
  0, 1, 1, 2, 2, 3, 3, 5, 4, 9, 5, 17
  -- </select3-6.3>
})

test:do_execsql_test("select3-6.4", [[
  SELECT log, min(n) FROM t1 GROUP BY log ORDER BY 1 DESC;
]], {
  -- <select3-6.4>
  5, 17, 4, 9, 3, 5, 2, 3, 1, 2, 0, 1
  -- </select3-6.4>
})

test:do_execsql_test("select3-6.5", [[
  CREATE INDEX i1 ON t1(log);
  SELECT log, min(n) FROM t1 GROUP BY log ORDER BY log;
]], {
  -- <select3-6.5>
  0, 1, 1, 2, 2, 3, 3, 5, 4, 9, 5, 17
  -- </select3-6.5>
})

test:do_execsql_test("select3-6.6", [[
  SELECT log, min(n) FROM t1 GROUP BY log ORDER BY log DESC;
]], {
  -- <select3-6.6>
  5, 17, 4, 9, 3, 5, 2, 3, 1, 2, 0, 1
  -- </select3-6.6>
})

test:do_execsql_test("select3-6.7", [[
  SELECT log, min(n) FROM t1 GROUP BY log ORDER BY 1;
]], {
  -- <select3-6.7>
  0, 1, 1, 2, 2, 3, 3, 5, 4, 9, 5, 17
  -- </select3-6.7>
})

test:do_execsql_test("select3-6.8", [[
  SELECT log, min(n) FROM t1 GROUP BY log ORDER BY 1 DESC;
]], {
  -- <select3-6.8>
  5, 17, 4, 9, 3, 5, 2, 3, 1, 2, 0, 1
  -- </select3-6.8>
})

-- Sometimes an aggregate query can return no rows at all.
--
test:do_execsql_test("select3-7.1", [[
  DROP TABLE IF EXISTS t2;
  CREATE TABLE t2(a  INT primary key,b INT );
  INSERT INTO t2 VALUES(1,2);
  SELECT a, sum(b) FROM t2 WHERE b=5 GROUP BY a;
]], {
  -- <select3-7.1>

  -- </select3-7.1>
})

test:do_execsql_test("select3-7.2", [[
  SELECT a, sum(b) FROM t2 WHERE b=5;
]], {
  -- <select3-7.2>
  "", ""
  -- </select3-7.2>
})

-- If a table column is of typeNUMBER but we are storing integer values
-- in it, the values are stored as integers to take up less space.  The
-- values are converted by toNUMBER as they are read out of the table.
-- Make sure the GROUP BY clause does this conversion correctly.
-- Ticket #2251.
--
test:do_execsql_test("select3-8.1", [[
  DROP TABLE IF EXISTS A;
  CREATE TABLE A (
    A1 DOUBLE,
    A2 TEXT,
    A3 DOUBLE,
    id int primary key
  );
  INSERT INTO A VALUES(39136,'ABC',1201900000, 1);
  INSERT INTO A VALUES(39136,'ABC',1207000000, 2);
  SELECT typeof(sum(a3)) FROM a;
]], {
  -- <select3-8.1>
  "double"
  -- </select3-8.1>
})

test:do_execsql_test("select3-8.2", [[
  SELECT typeof(sum(a3)) FROM a GROUP BY a1;
]], {
  -- <select3-8.2>
  "double"
  -- </select3-8.2>
})

test:finish_test()

