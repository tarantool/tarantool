#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(111)

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
-- This file implements regression tests for SQLite library.  The
-- focus of this file is testing the UPDATE statement.
--
-- $Id: update.test,v 1.19 2008/04/10 18:44:36 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Try to update an non-existent table
--
test:do_catchsql_test("update-1.1", [[
  UPDATE test1 SET f2=5 WHERE f1<1
]], {
  -- <update-1.1>
  1, "no such table: TEST1"
  -- </update-1.1>
})

-- # Try to update a read-only table
-- #
-- do_test update-2.1 {
--   set v [catch \
-- #        {execsql {UPDATE sqlite_master SET name='xyz' WHERE name='123'}} msg]
--   lappend v $msg
-- } {1 {table sqlite_master may not be modified}}
-- Create a table to work with
--
test:do_test("update-3.1", function()
  test:execsql "CREATE TABLE test1(id primary key, f1 int,f2 int)"
  -- for _ in X(0, "X!for", [=[["set i 1","$i<=10","incr i"]]=]) do
  for i = 1, 10 do    
    sql = string.format("INSERT INTO test1 VALUES(%s,%s,%s)", i, i, bit.lshift(1, i)) -- X(0, "X!expr", [=[["<<",1,["i"]]]=]))
    test:execsql(sql)
  end
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1"
end, {
  -- <update-3.1>
  1, 2, 2, 4, 3, 8, 4, 16, 5, 32, 6, 64, 7, 128, 8, 256, 9, 512, 10, 1024
  -- </update-3.1>
})

-- Unknown column name in an expression
--
test:do_catchsql_test("update-3.2", [[
  UPDATE test1 SET f1=f3*2 WHERE f2==32
]], {
  -- <update-3.2>
  1, "no such column: F3"
  -- </update-3.2>
})

test:do_catchsql_test("update-3.3", [[
  UPDATE test1 SET f1=test2.f1*2 WHERE f2==32
]], {
  -- <update-3.3>
  1, "no such column: TEST2.F1"
  -- </update-3.3>
})

test:do_catchsql_test("update-3.4", [[
  UPDATE test1 SET f3=f1*2 WHERE f2==32
]], {
  -- <update-3.4>
  1, "no such column: F3"
  -- </update-3.4>
})

-- Actually do some updates
--
test:do_execsql_test("update-3.5", [[
  UPDATE test1 SET f2=f2*3
]], {
  -- <update-3.5>
  
  -- </update-3.5>
})

-- Tarantool: we don't dupport this var export so far.
-- test:do_test("update-3.5.1", function()
--   return db("changes")
-- end, {
--   -- <update-3.5.1>
--   10
--   -- </update-3.5.1>
-- })

-- verify that SELECT does not reset the change counter
test:do_test("update-3.5.2", function()
  return test:db("eval", "SELECT count(*) FROM test1")
end, {
  -- <update-3.5.2>
  10
  -- </update-3.5.2>
})

-- Tarantool: see comment to -3.5.1
-- test:do_test("update-3.5.3", function()
--   return db("changes")
-- end, {
--   -- <update-3.5.3>
--   10
--   -- </update-3.5.3>
-- })

test:do_execsql_test("update-3.6", [[
  SELECT f1,f2 FROM test1 ORDER BY f1
]], {
  -- <update-3.6>
  1, 6, 2, 12, 3, 24, 4, 48, 5, 96, 6, 192, 7, 384, 8, 768, 9, 1536, 10, 3072
  -- </update-3.6>
})

test:do_test("update-3.7", function()
  test:execsql "PRAGMA count_changes=on"
  return test:execsql "UPDATE test1 SET f2=f2/3 WHERE f1<=5"
end, {
  -- <update-3.7>
  5
  -- </update-3.7>
})

test:do_execsql_test("update-3.8", [[
  SELECT f1,f2 FROM test1 ORDER BY f1
]], {
  -- <update-3.8>
  1, 2, 2, 4, 3, 8, 4, 16, 5, 32, 6, 192, 7, 384, 8, 768, 9, 1536, 10, 3072
  -- </update-3.8>
})

test:do_execsql_test("update-3.9", [[
  UPDATE test1 SET f2=f2/3 WHERE f1>5
]], {
  -- <update-3.9>
  5
  -- </update-3.9>
})

test:do_execsql_test("update-3.10", [[
  SELECT f1,f2 FROM test1 ORDER BY f1
]], {
  -- <update-3.10>
  1, 2, 2, 4, 3, 8, 4, 16, 5, 32, 6, 64, 7, 128, 8, 256, 9, 512, 10, 1024
  -- </update-3.10>
})

-- Swap the values of f1 and f2 for all elements
--
test:do_execsql_test("update-3.11", [[
  UPDATE test1 SET F2=f1, F1=f2
]], {
  -- <update-3.11>
  10
  -- </update-3.11>
})

test:do_execsql_test("update-3.12", [[
  SELECT f1,f2 FROM test1 ORDER BY F1
]], {
  -- <update-3.12>
  2, 1, 4, 2, 8, 3, 16, 4, 32, 5, 64, 6, 128, 7, 256, 8, 512, 9, 1024, 10
  -- </update-3.12>
})

test:do_test("update-3.13", function()
  test:execsql "PRAGMA count_changes=off"
  return test:execsql "UPDATE test1 SET F2=f1, F1=f2"
end, {
  -- <update-3.13>
  
  -- </update-3.13>
})

test:do_execsql_test("update-3.14", [[
  SELECT f1,f2 FROM test1 ORDER BY F1
]], {
  -- <update-3.14>
  1, 2, 2, 4, 3, 8, 4, 16, 5, 32, 6, 64, 7, 128, 8, 256, 9, 512, 10, 1024
  -- </update-3.14>
})

-- Create duplicate entries and make sure updating still
-- works.
--
test:do_test("update-4.0", function()
  test:execsql [[
    DELETE FROM test1 WHERE f1<=5;
    INSERT INTO test1(id,f1,f2) VALUES(20, 8,88);
    INSERT INTO test1(id,f1,f2) VALUES(21, 8,888);
    INSERT INTO test1(id,f1,f2) VALUES(22, 77,128);
    INSERT INTO test1(id,f1,f2) VALUES(23, 777,128);
  ]]
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-4.0>
  6, 64, 7, 128, 8, 88, 8, 256, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-4.0>
})

test:do_test("update-4.1", function()
  test:execsql "UPDATE test1 SET f2=f2+1 WHERE f1==8"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-4.1>
  6, 64, 7, 128, 8, 89, 8, 257, 8, 889, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-4.1>
})

test:do_test("update-4.2", function()
  test:execsql "UPDATE test1 SET f2=f2-1 WHERE f1==8 and f2>800"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-4.2>
  6, 64, 7, 128, 8, 89, 8, 257, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-4.2>
})

test:do_test("update-4.3", function()
  test:execsql "UPDATE test1 SET f2=f2-1 WHERE f1==8 and f2<800"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-4.3>
  6, 64, 7, 128, 8, 88, 8, 256, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-4.3>
})

test:do_test("update-4.4", function()
  test:execsql "UPDATE test1 SET f1=f1+1 WHERE f2==128"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-4.4>
  6, 64, 8, 88, 8, 128, 8, 256, 8, 888, 9, 512, 10, 1024, 78, 128, 778, 128
  -- </update-4.4>
})

test:do_test("update-4.5", function()
  test:execsql "UPDATE test1 SET f1=f1-1 WHERE f1>100 and f2==128"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-4.5>
  6, 64, 8, 88, 8, 128, 8, 256, 8, 888, 9, 512, 10, 1024, 78, 128, 777, 128
  -- </update-4.5>
})

test:do_execsql_test("update-4.6", [[
  PRAGMA count_changes=on;
  UPDATE test1 SET f1=f1-1 WHERE f1<=100 and f2==128;
]], {
  -- <update-4.6>
  2
  -- </update-4.6>
})

test:do_execsql_test("update-4.7", [[
  PRAGMA count_changes=off;
  SELECT f1,f2 FROM test1 ORDER BY f1,f2
]], {
  -- <update-4.7>
  6, 64, 7, 128, 8, 88, 8, 256, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-4.7>
})

-- Repeat the previous sequence of tests with an index.
--
test:do_test("update-5.0", function()
  test:execsql "CREATE INDEX idx1 ON test1(f1)"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-5.0>
  6, 64, 7, 128, 8, 88, 8, 256, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-5.0>
})

test:do_test("update-5.1", function()
  test:execsql "UPDATE test1 SET f2=f2+1 WHERE f1==8"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-5.1>
  6, 64, 7, 128, 8, 89, 8, 257, 8, 889, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-5.1>
})

test:do_test("update-5.2", function()
  test:execsql "UPDATE test1 SET f2=f2-1 WHERE f1==8 and f2>800"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-5.2>
  6, 64, 7, 128, 8, 89, 8, 257, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-5.2>
})

test:do_test("update-5.3", function()
  test:execsql "UPDATE test1 SET f2=f2-1 WHERE f1==8 and f2<800"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-5.3>
  6, 64, 7, 128, 8, 88, 8, 256, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-5.3>
})

test:do_test("update-5.4", function()
  test:execsql "UPDATE test1 SET f1=f1+1 WHERE f2==128"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-5.4>
  6, 64, 8, 88, 8, 128, 8, 256, 8, 888, 9, 512, 10, 1024, 78, 128, 778, 128
  -- </update-5.4>
})

test:do_execsql_test("update-5.4.1", [[
  SELECT f1,f2 FROM test1 WHERE f1==78 ORDER BY f1,f2
]], {
  -- <update-5.4.1>
  78, 128
  -- </update-5.4.1>
})

test:do_execsql_test("update-5.4.2", [[
  SELECT f1,f2 FROM test1 WHERE f1==778 ORDER BY f1,f2
]], {
  -- <update-5.4.2>
  778, 128
  -- </update-5.4.2>
})

test:do_execsql_test("update-5.4.3", [[
  SELECT f1,f2 FROM test1 WHERE f1==8 ORDER BY f1,f2
]], {
  -- <update-5.4.3>
  8, 88, 8, 128, 8, 256, 8, 888
  -- </update-5.4.3>
})

test:do_execsql_test("update-5.5", [[
  UPDATE test1 SET f1=f1-1 WHERE f1>100 and f2==128
]], {
  -- <update-5.5>
  
  -- </update-5.5>
})

test:do_execsql_test("update-5.5.1", [[
  SELECT f1,f2 FROM test1 ORDER BY f1,f2
]], {
  -- <update-5.5.1>
  6, 64, 8, 88, 8, 128, 8, 256, 8, 888, 9, 512, 10, 1024, 78, 128, 777, 128
  -- </update-5.5.1>
})

test:do_execsql_test("update-5.5.2", [[
  SELECT f1,f2 FROM test1 WHERE f1==78 ORDER BY f1,f2
]], {
  -- <update-5.5.2>
  78, 128
  -- </update-5.5.2>
})

test:do_execsql_test("update-5.5.3", [[
  SELECT f1,f2 FROM test1 WHERE f1==778 ORDER BY f1,f2
]], {
  -- <update-5.5.3>
  
  -- </update-5.5.3>
})

test:do_execsql_test("update-5.5.4", [[
  SELECT f1,f2 FROM test1 WHERE f1==777 ORDER BY f1,f2
]], {
  -- <update-5.5.4>
  777, 128
  -- </update-5.5.4>
})

test:do_execsql_test("update-5.5.5", [[
  SELECT f1,f2 FROM test1 WHERE f1==8 ORDER BY f1,f2
]], {
  -- <update-5.5.5>
  8, 88, 8, 128, 8, 256, 8, 888
  -- </update-5.5.5>
})

test:do_execsql_test("update-5.6", [[
  PRAGMA count_changes=on;
  UPDATE test1 SET f1=f1-1 WHERE f1<=100 and f2==128;
]], {
  -- <update-5.6>
  2
  -- </update-5.6>
})

test:do_execsql_test("update-5.6.1", [[
  PRAGMA count_changes=off;
  SELECT f1,f2 FROM test1 ORDER BY f1,f2
]], {
  -- <update-5.6.1>
  6, 64, 7, 128, 8, 88, 8, 256, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-5.6.1>
})

test:do_execsql_test("update-5.6.2", [[
  SELECT f1,f2 FROM test1 WHERE f1==77 ORDER BY f1,f2
]], {
  -- <update-5.6.2>
  77, 128
  -- </update-5.6.2>
})

test:do_execsql_test("update-5.6.3", [[
  SELECT f1,f2 FROM test1 WHERE f1==778 ORDER BY f1,f2
]], {
  -- <update-5.6.3>
  
  -- </update-5.6.3>
})

test:do_execsql_test("update-5.6.4", [[
  SELECT f1,f2 FROM test1 WHERE f1==777 ORDER BY f1,f2
]], {
  -- <update-5.6.4>
  777, 128
  -- </update-5.6.4>
})

test:do_execsql_test("update-5.6.5", [[
  SELECT f1,f2 FROM test1 WHERE f1==8 ORDER BY f1,f2
]], {
  -- <update-5.6.5>
  8, 88, 8, 256, 8, 888
  -- </update-5.6.5>
})

-- Repeat the previous sequence of tests with a different index.
--
--test:execsql "PRAGMA synchronous='FULL'"
test:do_test("update-6.0", function()
  test:execsql "DROP INDEX idx1 ON test1"
  test:execsql "CREATE INDEX idx1 ON test1(f2)"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-6.0>
  6, 64, 7, 128, 8, 88, 8, 256, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-6.0>
})

test:do_test("update-6.1", function()
  test:execsql "UPDATE test1 SET f2=f2+1 WHERE f1==8"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-6.1>
  6, 64, 7, 128, 8, 89, 8, 257, 8, 889, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-6.1>
})

test:do_execsql_test("update-6.1.1", [[
  SELECT f1,f2 FROM test1 WHERE f1==8 ORDER BY f1,f2
]], {
  -- <update-6.1.1>
  8, 89, 8, 257, 8, 889
  -- </update-6.1.1>
})

test:do_execsql_test("update-6.1.2", [[
  SELECT f1,f2 FROM test1 WHERE f2==89 ORDER BY f1,f2
]], {
  -- <update-6.1.2>
  8, 89
  -- </update-6.1.2>
})

test:do_execsql_test("update-6.1.3", [[
  SELECT f1,f2 FROM test1 WHERE f1==88 ORDER BY f1,f2
]], {
  -- <update-6.1.3>
  
  -- </update-6.1.3>
})

test:do_test("update-6.2", function()
  test:execsql "UPDATE test1 SET f2=f2-1 WHERE f1==8 and f2>800"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-6.2>
  6, 64, 7, 128, 8, 89, 8, 257, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-6.2>
})

test:do_test("update-6.3", function()
  test:execsql "UPDATE test1 SET f2=f2-1 WHERE f1==8 and f2<800"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-6.3>
  6, 64, 7, 128, 8, 88, 8, 256, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-6.3>
})

test:do_execsql_test("update-6.3.1", [[
  SELECT f1,f2 FROM test1 WHERE f1==8 ORDER BY f1,f2
]], {
  -- <update-6.3.1>
  8, 88, 8, 256, 8, 888
  -- </update-6.3.1>
})

test:do_execsql_test("update-6.3.2", [[
  SELECT f1,f2 FROM test1 WHERE f2==89 ORDER BY f1,f2
]], {
  -- <update-6.3.2>
  
  -- </update-6.3.2>
})

test:do_execsql_test("update-6.3.3", [[
  SELECT f1,f2 FROM test1 WHERE f2==88 ORDER BY f1,f2
]], {
  -- <update-6.3.3>
  8, 88
  -- </update-6.3.3>
})

test:do_test("update-6.4", function()
  test:execsql "UPDATE test1 SET f1=f1+1 WHERE f2==128"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-6.4>
  6, 64, 8, 88, 8, 128, 8, 256, 8, 888, 9, 512, 10, 1024, 78, 128, 778, 128
  -- </update-6.4>
})

test:do_execsql_test("update-6.4.1", [[
  SELECT f1,f2 FROM test1 WHERE f1==78 ORDER BY f1,f2
]], {
  -- <update-6.4.1>
  78, 128
  -- </update-6.4.1>
})

test:do_execsql_test("update-6.4.2", [[
  SELECT f1,f2 FROM test1 WHERE f1==778 ORDER BY f1,f2
]], {
  -- <update-6.4.2>
  778, 128
  -- </update-6.4.2>
})

test:do_execsql_test("update-6.4.3", [[
  SELECT f1,f2 FROM test1 WHERE f1==8 ORDER BY f1,f2
]], {
  -- <update-6.4.3>
  8, 88, 8, 128, 8, 256, 8, 888
  -- </update-6.4.3>
})

test:do_test("update-6.5", function()
  test:execsql "UPDATE test1 SET f1=f1-1 WHERE f1>100 and f2==128"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-6.5>
  6, 64, 8, 88, 8, 128, 8, 256, 8, 888, 9, 512, 10, 1024, 78, 128, 777, 128
  -- </update-6.5>
})

test:do_execsql_test("update-6.5.1", [[
  SELECT f1,f2 FROM test1 WHERE f1==78 ORDER BY f1,f2
]], {
  -- <update-6.5.1>
  78, 128
  -- </update-6.5.1>
})

test:do_execsql_test("update-6.5.2", [[
  SELECT f1,f2 FROM test1 WHERE f1==778 ORDER BY f1,f2
]], {
  -- <update-6.5.2>
  
  -- </update-6.5.2>
})

test:do_execsql_test("update-6.5.3", [[
  SELECT f1,f2 FROM test1 WHERE f1==777 ORDER BY f1,f2
]], {
  -- <update-6.5.3>
  777, 128
  -- </update-6.5.3>
})

test:do_execsql_test("update-6.5.4", [[
  SELECT f1,f2 FROM test1 WHERE f1==8 ORDER BY f1,f2
]], {
  -- <update-6.5.4>
  8, 88, 8, 128, 8, 256, 8, 888
  -- </update-6.5.4>
})

test:do_test("update-6.6", function()
  test:execsql "UPDATE test1 SET f1=f1-1 WHERE f1<=100 and f2==128"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-6.6>
  6, 64, 7, 128, 8, 88, 8, 256, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-6.6>
})

test:do_execsql_test("update-6.6.1", [[
  SELECT f1,f2 FROM test1 WHERE f1==77 ORDER BY f1,f2
]], {
  -- <update-6.6.1>
  77, 128
  -- </update-6.6.1>
})

test:do_execsql_test("update-6.6.2", [[
  SELECT f1,f2 FROM test1 WHERE f1==778 ORDER BY f1,f2
]], {
  -- <update-6.6.2>
  
  -- </update-6.6.2>
})

test:do_execsql_test("update-6.6.3", [[
  SELECT f1,f2 FROM test1 WHERE f1==777 ORDER BY f1,f2
]], {
  -- <update-6.6.3>
  777, 128
  -- </update-6.6.3>
})

test:do_execsql_test("update-6.6.4", [[
  SELECT f1,f2 FROM test1 WHERE f1==8 ORDER BY f1,f2
]], {
  -- <update-6.6.4>
  8, 88, 8, 256, 8, 888
  -- </update-6.6.4>
})

-- Repeat the previous sequence of tests with multiple
-- indices
--
test:do_test("update-7.0", function()
  test:execsql "CREATE INDEX idx2 ON test1(f2)"
  test:execsql "CREATE INDEX idx3 ON test1(f1,f2)"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-7.0>
  6, 64, 7, 128, 8, 88, 8, 256, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-7.0>
})

test:do_test("update-7.1", function()
  test:execsql "UPDATE test1 SET f2=f2+1 WHERE f1==8"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-7.1>
  6, 64, 7, 128, 8, 89, 8, 257, 8, 889, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-7.1>
})

test:do_execsql_test("update-7.1.1", [[
  SELECT f1,f2 FROM test1 WHERE f1==8 ORDER BY f1,f2
]], {
  -- <update-7.1.1>
  8, 89, 8, 257, 8, 889
  -- </update-7.1.1>
})

test:do_execsql_test("update-7.1.2", [[
  SELECT f1,f2 FROM test1 WHERE f2==89 ORDER BY f1,f2
]], {
  -- <update-7.1.2>
  8, 89
  -- </update-7.1.2>
})

test:do_execsql_test("update-7.1.3", [[
  SELECT f1,f2 FROM test1 WHERE f1==88 ORDER BY f1,f2
]], {
  -- <update-7.1.3>
  
  -- </update-7.1.3>
})

test:do_test("update-7.2", function()
  test:execsql "UPDATE test1 SET f2=f2-1 WHERE f1==8 and f2>800"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-7.2>
  6, 64, 7, 128, 8, 89, 8, 257, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-7.2>
})

test:do_test("update-7.3", function()
  -- explain {UPDATE test1 SET f2=f2-1 WHERE f1==8 and F2<300}
  test:execsql "UPDATE test1 SET f2=f2-1 WHERE f1==8 and f2<800"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-7.3>
  6, 64, 7, 128, 8, 88, 8, 256, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-7.3>
})

test:do_execsql_test("update-7.3.1", [[
  SELECT f1,f2 FROM test1 WHERE f1==8 ORDER BY f1,f2
]], {
  -- <update-7.3.1>
  8, 88, 8, 256, 8, 888
  -- </update-7.3.1>
})

test:do_execsql_test("update-7.3.2", [[
  SELECT f1,f2 FROM test1 WHERE f2==89 ORDER BY f1,f2
]], {
  -- <update-7.3.2>
  
  -- </update-7.3.2>
})

test:do_execsql_test("update-7.3.3", [[
  SELECT f1,f2 FROM test1 WHERE f2==88 ORDER BY f1,f2
]], {
  -- <update-7.3.3>
  8, 88
  -- </update-7.3.3>
})

test:do_test("update-7.4", function()
  test:execsql "UPDATE test1 SET f1=f1+1 WHERE f2==128"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-7.4>
  6, 64, 8, 88, 8, 128, 8, 256, 8, 888, 9, 512, 10, 1024, 78, 128, 778, 128
  -- </update-7.4>
})

test:do_execsql_test("update-7.4.1", [[
  SELECT f1,f2 FROM test1 WHERE f1==78 ORDER BY f1,f2
]], {
  -- <update-7.4.1>
  78, 128
  -- </update-7.4.1>
})

test:do_execsql_test("update-7.4.2", [[
  SELECT f1,f2 FROM test1 WHERE f1==778 ORDER BY f1,f2
]], {
  -- <update-7.4.2>
  778, 128
  -- </update-7.4.2>
})

test:do_execsql_test("update-7.4.3", [[
  SELECT f1,f2 FROM test1 WHERE f1==8 ORDER BY f1,f2
]], {
  -- <update-7.4.3>
  8, 88, 8, 128, 8, 256, 8, 888
  -- </update-7.4.3>
})

test:do_test("update-7.5", function()
  test:execsql "UPDATE test1 SET f1=f1-1 WHERE f1>100 and f2==128"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-7.5>
  6, 64, 8, 88, 8, 128, 8, 256, 8, 888, 9, 512, 10, 1024, 78, 128, 777, 128
  -- </update-7.5>
})

test:do_execsql_test("update-7.5.1", [[
  SELECT f1,f2 FROM test1 WHERE f1==78 ORDER BY f1,f2
]], {
  -- <update-7.5.1>
  78, 128
  -- </update-7.5.1>
})

test:do_execsql_test("update-7.5.2", [[
  SELECT f1,f2 FROM test1 WHERE f1==778 ORDER BY f1,f2
]], {
  -- <update-7.5.2>
  
  -- </update-7.5.2>
})

test:do_execsql_test("update-7.5.3", [[
  SELECT f1,f2 FROM test1 WHERE f1==777 ORDER BY f1,f2
]], {
  -- <update-7.5.3>
  777, 128
  -- </update-7.5.3>
})

test:do_execsql_test("update-7.5.4", [[
  SELECT f1,f2 FROM test1 WHERE f1==8 ORDER BY f1,f2
]], {
  -- <update-7.5.4>
  8, 88, 8, 128, 8, 256, 8, 888
  -- </update-7.5.4>
})

test:do_test("update-7.6", function()
  test:execsql "UPDATE test1 SET f1=f1-1 WHERE f1<=100 and f2==128"
  return test:execsql "SELECT f1,f2 FROM test1 ORDER BY f1,f2"
end, {
  -- <update-7.6>
  6, 64, 7, 128, 8, 88, 8, 256, 8, 888, 9, 512, 10, 1024, 77, 128, 777, 128
  -- </update-7.6>
})

test:do_execsql_test("update-7.6.1", [[
  SELECT f1,f2 FROM test1 WHERE f1==77 ORDER BY f1,f2
]], {
  -- <update-7.6.1>
  77, 128
  -- </update-7.6.1>
})

test:do_execsql_test("update-7.6.2", [[
  SELECT f1,f2 FROM test1 WHERE f1==778 ORDER BY f1,f2
]], {
  -- <update-7.6.2>
  
  -- </update-7.6.2>
})

test:do_execsql_test("update-7.6.3", [[
  SELECT f1,f2 FROM test1 WHERE f1==777 ORDER BY f1,f2
]], {
  -- <update-7.6.3>
  777, 128
  -- </update-7.6.3>
})

test:do_execsql_test("update-7.6.4", [[
  SELECT f1,f2 FROM test1 WHERE f1==8 ORDER BY f1,f2
]], {
  -- <update-7.6.4>
  8, 88, 8, 256, 8, 888
  -- </update-7.6.4>
})

-- Error messages
--
test:do_catchsql_test("update-9.1", [[
  UPDATE test1 SET x=11 WHERE f1=1025
]], {
  -- <update-9.1>
  1, "no such column: X"
  -- </update-9.1>
})

test:do_catchsql_test("update-9.2", [[
  UPDATE test1 SET f1=x(11) WHERE f1=1025
]], {
  -- <update-9.2>
  1, "no such function: X"
  -- </update-9.2>
})

test:do_catchsql_test("update-9.3", [[
  UPDATE test1 SET f1=11 WHERE x=1025
]], {
  -- <update-9.3>
  1, "no such column: X"
  -- </update-9.3>
})

test:do_catchsql_test("update-9.4", [[
  UPDATE test1 SET f1=11 WHERE x(f1)=1025
]], {
  -- <update-9.4>
  1, "no such function: X"
  -- </update-9.4>
})

-- Try doing updates on a unique column where the value does not
-- really change.
--
test:do_execsql_test("update-10.1", [[
  DROP TABLE test1;
  CREATE TABLE t1(
     a integer primary key,
     b UNIQUE, 
     c, d,
     e, f,
     UNIQUE(c,d)
  );
  INSERT INTO t1 VALUES(1,2,3,4,5,6);
  INSERT INTO t1 VALUES(2,3,4,4,6,7);
  SELECT * FROM t1
]], {
  -- <update-10.1>
  1, 2, 3, 4, 5, 6, 2, 3, 4, 4, 6, 7
  -- </update-10.1>
})

test:do_catchsql_test("update-10.2", [[
  UPDATE t1 SET a=1, e=9 WHERE f=6;
  SELECT * FROM t1;
]], {
  -- <update-10.2>
  0, {1, 2, 3, 4, 9, 6, 2, 3, 4, 4, 6, 7}
  -- </update-10.2>
})

test:do_catchsql_test("update-10.3", [[
  UPDATE t1 SET a=1, e=10 WHERE f=7;
  SELECT * FROM t1;
]], {
  -- <update-10.3>
  1, "Duplicate key exists in unique index 'sqlite_autoindex_T1_3' in space 'T1'"
  -- </update-10.3>
})

test:do_catchsql_test("update-10.4", [[
  SELECT * FROM t1;
]], {
  -- <update-10.4>
  0, {1, 2, 3, 4, 9, 6, 2, 3, 4, 4, 6, 7}
  -- </update-10.4>
})

test:do_catchsql_test("update-10.5", [[
  UPDATE t1 SET b=2, e=11 WHERE f=6;
  SELECT * FROM t1;
]], {
  -- <update-10.5>
  0, {1, 2, 3, 4, 11, 6, 2, 3, 4, 4, 6, 7}
  -- </update-10.5>
})

test:do_catchsql_test("update-10.6", [[
  UPDATE t1 SET b=2, e=12 WHERE f=7;
  SELECT * FROM t1;
]], {
  -- <update-10.6>
  1, "Duplicate key exists in unique index 'sqlite_autoindex_T1_1' in space 'T1'"
  -- </update-10.6>
})

test:do_catchsql_test("update-10.7", [[
  SELECT * FROM t1;
]], {
  -- <update-10.7>
  0, {1, 2, 3, 4, 11, 6, 2, 3, 4, 4, 6, 7}
  -- </update-10.7>
})

test:do_catchsql_test("update-10.8", [[
  UPDATE t1 SET c=3, d=4, e=13 WHERE f=6;
  SELECT * FROM t1;
]], {
  -- <update-10.8>
  0, {1, 2, 3, 4, 13, 6, 2, 3, 4, 4, 6, 7}
  -- </update-10.8>
})

test:do_catchsql_test("update-10.9", [[
  UPDATE t1 SET c=3, d=4, e=14 WHERE f=7;
  SELECT * FROM t1;
]], {
  -- <update-10.9>
  1, "Duplicate key exists in unique index 'sqlite_autoindex_T1_2' in space 'T1'"
  -- </update-10.9>
})

test:do_catchsql_test("update-10.10", [[
  SELECT * FROM t1;
]], {
  -- <update-10.10>
  0, {1, 2, 3, 4, 13, 6, 2, 3, 4, 4, 6, 7}
  -- </update-10.10>
})

-- Make sure we can handle a subquery in the where clause.
--
-- if X(0, "X!capable", [["subquery"]]) then
  test:do_execsql_test("update-11.1", [[
    UPDATE t1 SET e=e+1 WHERE b IN (SELECT b FROM t1);
    SELECT b,e FROM t1;
  ]], {
    -- <update-11.1>
    2, 14, 3, 7
    -- </update-11.1>
  })

  test:do_execsql_test("update-11.2", [[
    UPDATE t1 SET e=e+1 WHERE a IN (SELECT a FROM t1);
    SELECT a,e FROM t1;
  ]], {
    -- <update-11.2>
    1, 15, 2, 8
    -- </update-11.2>
  })

-- end

-- integrity_check update-12.1
-- MUST_WORK_TEST now works very very slowwwwwwwww, more than few minutes
-- # Ticket 602.  Updates should occur in the same order as the records
-- # were discovered in the WHERE clause.
-- #
-- do_test update-13.1 {
--   execsql {
--     BEGIN;
--     CREATE TABLE t2(id primary key, a);
--     INSERT INTO t2 VALUES(1, 1);
--     INSERT INTO t2 VALUES(2, 2);
--     INSERT INTO t2 SELECT id+2,a+2 FROM t2;
--     INSERT INTO t2 SELECT id+4,a+4 FROM t2;
--     INSERT INTO t2 SELECT id+8,a+8 FROM t2;
--     INSERT INTO t2 SELECT id+16,a+16 FROM t2;
--     INSERT INTO t2 SELECT id+32,a+32 FROM t2;
--     INSERT INTO t2 SELECT id+64,a+64 FROM t2;
--     INSERT INTO t2 SELECT id+128,a+128 FROM t2;
--     INSERT INTO t2 SELECT id+256,a+256 FROM t2;
--     INSERT INTO t2 SELECT id+512,a+512 FROM t2;
--     INSERT INTO t2 SELECT id+1024,a+1024 FROM t2;
--     COMMIT;
--     SELECT count(*) FROM t2;
--   }
-- } {2048}
-- do_test update-13.2 {
--   execsql {
--     SELECT count(*) FROM t2 WHERE a=id;
--   }
-- } {2048}
-- do_test update-13.3 {
--   execsql {
--     UPDATE t2 SET id=id-1;
--     SELECT count(*) FROM t2 WHERE a=id+1;
--   }
-- } {2048}
-- do_test update-13.3 {
--   execsql {
--     UPDATE t2 SET id=id+10000;
--     UPDATE t2 SET id=id-9999;
--     SELECT count(*) FROM t2 WHERE a=id;
--   }
-- } {2048}
-- do_test update-13.4 {
--   execsql {
--     BEGIN;
--     INSERT INTO t2 SELECT id+2048,a+2048 FROM t2;
--     INSERT INTO t2 SELECT id+4096,a+4096 FROM t2;
--     INSERT INTO t2 SELECT id+8192,a+8192 FROM t2;
--     SELECT count(*) FROM t2 WHERE a=id;
--     COMMIT;
--   }
-- } 16384
-- do_test update-13.5 {
--   execsql {
--     UPDATE t2 SET id=id-1;
--     SELECT count(*) FROM t2 WHERE a=id+1;
--   }
-- } 16384
-- integrity_check update-13.6
-- MUST_WORK_TEST
-- ifcapable {trigger} {
-- # Test for proper detection of malformed WHEN clauses on UPDATE triggers.
-- #
-- do_test update-14.1 {
--   execsql {
--     CREATE TABLE t3(a,b,c);
--     CREATE TRIGGER t3r1 BEFORE UPDATE on t3 WHEN nosuchcol BEGIN
--       SELECT 'illegal WHEN clause';
--     END;
--   }
-- } {}
-- do_test update-14.2 {
--   catchsql {
--     UPDATE t3 SET a=1;
--   }
-- } {1 {no such column: nosuchcol}}
-- do_test update-14.3 {
--   execsql {
--     CREATE TABLE t4(a,b,c);
--     CREATE TRIGGER t4r1 AFTER UPDATE on t4 WHEN nosuchcol BEGIN
--       SELECT 'illegal WHEN clause';
--     END;
--   }
-- } {}
-- do_test update-14.4 {
--   catchsql {
--     UPDATE t4 SET a=1;
--   }
-- } {1 {no such column: nosuchcol}}
-- } ;# ifcapable {trigger}
-- MUST_WORK_TEST
-- # Ticket [https://www.sqlite.org/src/tktview/43107840f1c02] on 2014-10-29
-- # An assertion fault on UPDATE
-- #
-- do_execsql_test update-15.1 {
--   CREATE TABLE t15(a INTEGER PRIMARY KEY, b);
--   INSERT INTO t15(a,b) VALUES(10,'abc'),(20,'def'),(30,'ghi');
--   ALTER TABLE t15 ADD COLUMN c;
--   CREATE INDEX t15c ON t15(c);
--   INSERT INTO t15(a,b)
--    VALUES(5,'zyx'),(15,'wvu'),(25,'tsr'),(35,'qpo');
--   UPDATE t15 SET c=printf("y%d",a) WHERE c IS NULL;
--   SELECT a,b,c,'|' FROM t15 ORDER BY a;
-- } {5 zyx y5 | 10 abc y10 | 15 wvu y15 | 20 def y20 | 25 tsr y25 | 30 ghi y30 | 35 qpo y35 |}

test:do_execsql_test(
    "insert-15.0",
    [[
        create table test(a primary key);
        insert into test(a) values(1);
    ]])

test:do_catchsql_test(
    "insert-15.1",
    [[
        update test set a = 2, a = 3;
    ]],
    {1, "set id list: duplicate column name A"})

test:do_execsql_test(
  "insert-15.2",
  [[
      drop table test;
  ]])

test:finish_test()

