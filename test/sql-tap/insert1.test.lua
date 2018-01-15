#!/usr/bin/env tarantool
test = require("sqltester")
yaml = require('yaml')
test:plan(28)

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
-- focus of this file is testing the INSERT statement.
--
-- $Id: insert.test,v 1.31 2007/04/05 11:25:59 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Try to insert into a non-existant table.
--
test:do_catchsql_test("insert-1.1", [[
  INSERT INTO test1 VALUES(1,2,3)
]], {
  -- <insert-1.1>
  1, "no such table: TEST1"
  -- </insert-1.1>
})

-- # Try to insert into sqlite_master
-- #
-- do_test insert-1.2 {
--   set v [catch {execsql {INSERT INTO sqlite_master VALUES(1,2,3,4)}} msg]
--   lappend v $msg
-- } {1 {table sqlite_master may not be modified}}
-- Try to insert the wrong number of entries.
--
test:do_test("insert-1.3", function()
  test:execsql "CREATE TABLE test1(one int PRIMARY KEY, two int, three int)"
  return test:catchsql "INSERT INTO test1 VALUES(1,2)"
end, {
  -- <insert-1.3>
  1, "table TEST1 has 3 columns but 2 values were supplied"
  -- </insert-1.3>
})

test:do_catchsql_test("insert-1.3b", [[
  INSERT INTO test1 VALUES(1,2,3,4)
]], {
  -- <insert-1.3b>
  1, "table TEST1 has 3 columns but 4 values were supplied"
  -- </insert-1.3b>
})

test:do_catchsql_test("insert-1.3c", [[
  INSERT INTO test1(one,two) VALUES(1,2,3,4)
]], {
  -- <insert-1.3c>
  1, "4 values for 2 columns"
  -- </insert-1.3c>
})

test:do_catchsql_test("insert-1.3d", [[
  INSERT INTO test1(one,two) VALUES(1)
]], {
  -- <insert-1.3d>
  1, "1 values for 2 columns"
  -- </insert-1.3d>
})

-- Try to insert into a non-existant column of a table.
--
test:do_catchsql_test("insert-1.4", [[
  INSERT INTO test1(one,four) VALUES(1,2)
]], {
  -- <insert-1.4>
  1, "table TEST1 has no column named FOUR"
  -- </insert-1.4>
})

-- Make sure the inserts actually happen
--
test:do_test("insert-1.5", function()
  test:execsql "INSERT INTO test1 VALUES(1,2,3)"
  return test:execsql "SELECT * FROM test1"
end, {
  -- <insert-1.5>
  1, 2, 3
  -- </insert-1.5>
})

test:do_test("insert-1.5b", function()
  test:execsql "INSERT INTO test1 VALUES(4,5,6)"
  return test:execsql "SELECT * FROM test1 ORDER BY one"
end, {
  -- <insert-1.5b>
  1, 2, 3, 4, 5, 6
  -- </insert-1.5b>
})

test:do_test("insert-1.5c", function()
  test:execsql "INSERT INTO test1 VALUES(7,8,9)"
  return test:execsql "SELECT * FROM test1 ORDER BY one"
end, {
  -- <insert-1.5c>
  1, 2, 3, 4, 5, 6, 7, 8, 9
  -- </insert-1.5c>
})

test:do_test("insert-1.6", function()
  test:execsql "DELETE FROM test1"
  test:execsql "INSERT INTO test1(one,two) VALUES(1,2)"
  return test:execsql "SELECT * FROM test1 ORDER BY one"
end, {
  -- <insert-1.6>
  1, 2, ""
  -- </insert-1.6>
})

-- MUST_WORK_TEST
-- Tarantool: `one` is PK now, so NULL is prohibited.
-- comment test, fix expected result.
--do_test insert-1.6b {
--  execsql {INSERT INTO test1(two,three) VALUES(5,6)}
--  execsql {SELECT * FROM test1 ORDER BY one}
--} {{} 5 6 1 2 {}}
test:do_test("insert-1.6c", function()
  test:execsql "INSERT INTO test1(three,one) VALUES(7,8)"
  return test:execsql "SELECT * FROM test1 ORDER BY one"
end, {
  -- <insert-1.6c>
  1, 2, "", 8, "", 7
  -- </insert-1.6c>
})

-- MUST_WORK_TEST
-- # A table to use for testing default values
-- #
-- do_test insert-2.1 {
--   execsql {
--     CREATE TABLE test2(
--       f1 int default -111, 
--       f2 real default +4.32,
--       f3 int default +222,
--       f4 int default 7.89
--     )
--   }
--   execsql {SELECT * from test2}
-- } {}
-- do_test insert-2.2 {
--   execsql {INSERT INTO test2(f1,f3) VALUES(+10,-10)}
--   execsql {SELECT * FROM test2}
-- } {10 4.32 -10 7.89}
-- do_test insert-2.3 {
--   execsql {INSERT INTO test2(f2,f4) VALUES(1.23,-3.45)}
--   execsql {SELECT * FROM test2 WHERE f1==-111}
-- } {-111 1.23 222 -3.45}
-- do_test insert-2.4 {
--   execsql {INSERT INTO test2(f1,f2,f4) VALUES(77,+1.23,3.45)}
--   execsql {SELECT * FROM test2 WHERE f1==77}
-- } {77 1.23 222 3.45}
-- do_test insert-2.10 {
--   execsql {
--     DROP TABLE test2;
--     CREATE TABLE test2(
--       f1 int default 111, 
--       f2 real default -4.32,
--       f3 text default hi,
--       f4 text default 'abc-123',
--       f5 varchar(10)
--     )
--   }
--   execsql {SELECT * from test2}
-- } {}
-- do_test insert-2.11 {
--   execsql {INSERT INTO test2(f2,f4) VALUES(-2.22,'hi!')}
--   execsql {SELECT * FROM test2}
-- } {111 -2.22 hi hi! {}}
-- do_test insert-2.12 {
--   execsql {INSERT INTO test2(f1,f5) VALUES(1,'xyzzy')}
--   execsql {SELECT * FROM test2 ORDER BY f1}
-- } {1 -4.32 hi abc-123 xyzzy 111 -2.22 hi hi! {}}
-- # Do additional inserts with default values, but this time
-- # on a table that has indices.  In particular we want to verify
-- # that the correct default values are inserted into the indices.
-- #
-- do_test insert-3.1 {
--   execsql {
--     DELETE FROM test2;
--     CREATE INDEX index9 ON test2(f1,f2);
--     CREATE INDEX indext ON test2(f4,f5);
--     SELECT * from test2;
--   }
-- } {}
-- # Update for sqlite3 v3:
-- # Change the 111 to '111' in the following two test cases, because
-- # the default value is being inserted as a string. TODO: It shouldn't be.
-- do_test insert-3.2 {
--   execsql {INSERT INTO test2(f2,f4) VALUES(-3.33,'hum')}
--   execsql {SELECT * FROM test2 WHERE f1='111' AND f2=-3.33}
-- } {111 -3.33 hi hum {}}
-- do_test insert-3.3 {
--   execsql {INSERT INTO test2(f1,f2,f5) VALUES(22,-4.44,'wham')}
--   execsql {SELECT * FROM test2 WHERE f1='111' AND f2=-3.33}
-- } {111 -3.33 hi hum {}}
-- do_test insert-3.4 {
--   execsql {SELECT * FROM test2 WHERE f1=22 AND f2=-4.44}
-- } {22 -4.44 hi abc-123 wham}
-- ifcapable {reindex} {
--   do_test insert-3.5 {
--     execsql REINDEX
--   } {}
-- }
-- integrity_check insert-3.5
-- Test of expressions in the VALUES clause
--
test:do_execsql_test("insert-4.1", [[
  CREATE TABLE t3(a PRIMARY KEY,b,c);
  INSERT INTO t3 VALUES(1+2+3,4,5);
  SELECT * FROM t3;
]], {
  -- <insert-4.1>
  6, 4, 5
  -- </insert-4.1>
})

test:do_test("insert-4.2", function()
  test:execsql "INSERT INTO t3 VALUES((SELECT max(a) FROM t3)+1,5,6);"

  return test:execsql [[
    SELECT * FROM t3 ORDER BY a;
  ]]
end, {
  -- <insert-4.2>
  6, 4, 5, 7, 5, 6
  -- </insert-4.2>
})

-- if X(0, "X!capable", [["subquery"]]) then
  test:do_catchsql_test("insert-4.3", [[
    INSERT INTO t3 VALUES((SELECT max(a) FROM t3)+1,t3.a,6);
    SELECT * FROM t3 ORDER BY a;
  ]], {
    -- <insert-4.3>
    1, "no such column: T3.A"
    -- </insert-4.3>
  })

-- end

-- Tarantool: a is PK now, so NULL is prohibited. Comment tests
--do_test insert-4.4 {
--  ifcapable subquery {
--    execsql {INSERT INTO t3 VALUES((SELECT b FROM t3 WHERE a=0),6,7);}
--  } else {
--    set b [execsql {SELECT b FROM t3 WHERE a = 0}]
--    if {$b==""} {set b NULL}
--    execsql "INSERT INTO t3 VALUES($b,6,7);"
--  }
--  execsql {
--    SELECT * FROM t3 ORDER BY a;
--  }
--} {{} 6 7 6 4 5 7 5 6}
--do_test insert-4.5 {
--  execsql {
--    SELECT b,c FROM t3 WHERE a IS NULL;
--  }
--} {6 7}
test:do_catchsql_test("insert-4.6", [[
  INSERT INTO t3 VALUES(notafunc(2,3),2,3);
]], {
  -- <insert-4.6>
  1, "no such function: NOTAFUNC"
  -- </insert-4.6>
})

test:do_execsql_test("insert-4.7", [[
  INSERT INTO t3 VALUES(min(1,2,3),max(1,2,3),99);
  SELECT * FROM t3 WHERE c=99;
]], {
  -- <insert-4.7>
  1, 3, 99
  -- </insert-4.7>
})

-- Test the ability to insert from a temporary table into itself.
-- Ticket #275.
--
-- if X(0, "X!capable", [["tempdb"]]) then
  test:do_execsql_test("insert-5.1", [[
    CREATE TABLE t4(x PRIMARY KEY);
    INSERT INTO t4 VALUES(1);
    SELECT * FROM t4;
  ]], {
    -- <insert-5.1>
    1
    -- </insert-5.1>
  })

  test:do_execsql_test("insert-5.2", [[
    INSERT INTO t4 SELECT x+1 FROM t4;
    SELECT * FROM t4;
  ]], {
    -- <insert-5.2>
    1, 2
    -- </insert-5.2>
  })

  -- if X(0, "X!capable", [["explain"]]) then
    test:do_test("insert-5.3", function()
      -- verify that a temporary table is used to copy t4 to t4
      x = test:execsql [[
        EXPLAIN INSERT INTO t4 SELECT x+2 FROM t4;
      ]]
      return {test:lsearch(x, 'OpenTEphemeral') > 0} -- X(264, "X!cmd", [=[["expr","[lsearch $x OpenEphemeral]>0"]]=])
    end, {
      -- <insert-5.3>
      1
      -- </insert-5.3>
    })

  -- end

  -- do_test insert-5.5 {
  --   # Verify that "t4" begins on page 3.
  --   #
  --   # Update for v3 - the first table now begins on page 2 of each file, not 3.
  --   execsql {
  --     SELECT rootpage FROM sqlite_temp_master WHERE name='t4';
  --   }
  -- } {2}
  test:do_test("insert-5.6", function()
    -- This should not use an intermediate temporary table.
    return test:execsql [[
      INSERT INTO t4 SELECT one FROM test1 WHERE three=7;
      SELECT * FROM t4
    ]]
  end, {
    -- <insert-5.6>
    1, 2, 8
    -- </insert-5.6>
  })

  -- if X(0, "X!capable", [["explain"]]) then
    test:do_test("insert-5.7", function()
      -- verify that no temporary table is used to copy test1 to t4
      x = test:execsql [[
        EXPLAIN INSERT INTO t4 SELECT one FROM test1;
      ]]
      return {test:lsearch(x, 'OpenTemp') > 0} -- X(298, "X!cmd", [=[["expr","[lsearch $x OpenTemp]>0"]]=])
    end, {
      -- <insert-5.7>
      0
      -- </insert-5.7>
    })

  -- end

-- end

-- Ticket #334:  REPLACE statement corrupting indices.
--
-- if X(0, "X!capable", [["conflict"]]) then
  -- The REPLACE command is not available if SQLITE_OMIT_CONFLICT is 
  -- defined at compilation time.
  test:do_execsql_test("insert-6.1", [[
    CREATE TABLE t1(a INTEGER PRIMARY KEY, b UNIQUE);
    INSERT INTO t1 VALUES(1,2);
    INSERT INTO t1 VALUES(2,3);
    SELECT b FROM t1 WHERE b=2;
  ]], {
    -- <insert-6.1>
    2
    -- </insert-6.1>
  })

  -- MUST_WORK_TEST
  -- do_test insert-6.2 {
  --   execsql {
  --     REPLACE INTO t1 VALUES(1,4);
  --     SELECT b FROM t1 WHERE b=2;
  --   }
  -- } {}
  -- do_test insert-6.3 {
  --   execsql {
  --     UPDATE OR REPLACE t1 SET a=2 WHERE b=4;
  --     SELECT * FROM t1 WHERE b=4;
  --   }
  -- } {2 4}
  -- do_test insert-6.4 {
  --   execsql {
  --     SELECT * FROM t1 WHERE b=3;
  --   }
  -- } {}
  -- ifcapable {reindex} {
  --   do_test insert-6.5 {
  --     execsql REINDEX
  --   } {}
  -- }
  -- do_test insert-6.6 {
  --   execsql {
  --     DROP TABLE t1;
  --   }
  -- } {}
-- end

-- # Test that the special optimization for queries of the form 
-- # "SELECT max(x) FROM tbl" where there is an index on tbl(x) works with 
-- # INSERT statments.
-- do_test insert-7.1 {
--   execsql {
--     CREATE TABLE t1(a);
--     INSERT INTO t1 VALUES(1);
--     INSERT INTO t1 VALUES(2);
--     CREATE INDEX i1 ON t1(a);
--   }
-- } {}
-- do_test insert-7.2 {
--   execsql {
--     INSERT INTO t1 SELECT max(a) FROM t1;
--   }
-- } {}
-- do_test insert-7.3 {
--   execsql {
--     SELECT a FROM t1;
--   }
-- } {1 2 2}
-- # Ticket #1140:  Check for an infinite loop in the algorithm that tests
-- # to see if the right-hand side of an INSERT...SELECT references the left-hand
-- # side.
-- #
-- ifcapable subquery&&compound {
--   do_test insert-8.1 {
--     execsql {
--       INSERT INTO t3 SELECT * FROM (SELECT * FROM t3 UNION ALL SELECT 1,2,3)
--     }
--   } {}
-- }
-- # Make sure the rowid cache in the VDBE is reset correctly when
-- # an explicit rowid is given.
-- #
-- do_test insert-9.1 {
--   execsql {
--     CREATE TABLE t5(x);
--     INSERT INTO t5 VALUES(1);
--     INSERT INTO t5 VALUES(2);
--     INSERT INTO t5 VALUES(3);
--     INSERT INTO t5(rowid, x) SELECT nullif(x*2+10,14), x+100 FROM t5;
--     SELECT rowid, x FROM t5;
--   }
-- } {1 1 2 2 3 3 12 101 13 102 16 103}
-- MUST_WORK_TEST
-- do_test insert-9.2 {
--   execsql {
--     CREATE TABLE t6(x INTEGER PRIMARY KEY, y);
--     INSERT INTO t6 VALUES(1,1);
--     INSERT INTO t6 VALUES(2,2);
--     INSERT INTO t6 VALUES(3,3);
--     INSERT INTO t6 SELECT nullif(y*2+10,14), y+100 FROM t6;
--     SELECT x, y FROM t6;
--   }
-- } {1 1 2 2 3 3 12 101 13 102 16 103}
-- Multiple VALUES clauses
--
-- if X(0, "X!capable", [["compound"]]) then
  test:do_execsql_test("insert-10.1", [[
    CREATE TABLE t10(a PRIMARY KEY,b,c);
    INSERT INTO t10 VALUES(1,2,3), (4,5,6), (7,8,9);
    SELECT * FROM t10;
  ]], {
    -- <insert-10.1>
    1, 2, 3, 4, 5, 6, 7, 8, 9
    -- </insert-10.1>
  })

  test:do_catchsql_test("insert-10.2", [[
    INSERT INTO t10 VALUES(11,12,13), (14,15), (16,17,28);
  ]], {
    -- <insert-10.2>
    1, "all VALUES must have the same number of terms"
    -- </insert-10.2>
  })

-- end

-- # Need for the OP_SoftNull opcode
-- #
-- do_execsql_test insert-11.1 {
--   CREATE TABLE t11a AS SELECT '123456789' AS x;
--   CREATE TABLE t11b (a INTEGER PRIMARY KEY, b, c);
--   INSERT INTO t11b SELECT x, x, x FROM t11a;
--   SELECT quote(a), quote(b), quote(c) FROM t11b;
-- } {123456789 '123456789' '123456789'}
-- # More columns of input than there are columns in the table.
-- # Ticket http://www.sqlite.org/src/info/e9654505cfda9361
-- #
-- do_execsql_test insert-12.1 {
--   CREATE TABLE t12a(a,b,c,d,e,f,g);
--   INSERT INTO t12a VALUES(101,102,103,104,105,106,107);
--   CREATE TABLE t12b(x);
--   INSERT INTO t12b(x,rowid,x,x,x,x,x) SELECT * FROM t12a;
--   SELECT rowid, x FROM t12b;
-- } {102 101}
-- do_execsql_test insert-12.2 {
--   CREATE TABLE tab1( value INTEGER);
--   INSERT INTO tab1 (value, _rowid_) values( 11, 1);
--   INSERT INTO tab1 (value, _rowid_) SELECT 22,999;
--   SELECT * FROM tab1;
-- } {11 22}
-- do_execsql_test insert-12.3 {
--   CREATE TABLE t12c(a, b DEFAULT 'xyzzy', c);
--   INSERT INTO t12c(a, rowid, c) SELECT 'one', 999, 'two';
--   SELECT * FROM t12c;
-- } {one xyzzy two}
-- integrity_check insert-99.0


test:do_execsql_test(
    "insert-13.0",
    [[
        create table test(a primary key, b)
    ]])

test:do_catchsql_test(
    "insert-13.1",
    [[
        insert into test(a, a, b) values(1, 1, 1)
    ]],
    {1, "table id list: duplicate column name A"})

test:do_catchsql_test(
    "insert-13.2",
    [[
        insert into test(a, b, b) values(1, 1, 1)
    ]],
    {1, "table id list: duplicate column name B"})

test:do_execsql_test(
    "insert-13.3",
    [[
        drop table test;
    ]])

test:finish_test()

