#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(8)

--!./tcltestrunner.lua
-- 2006 October 27
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
-- focus of this file is testing the use of indices in WHERE clauses.
-- This file was created when support for optimizing IS NULL phrases
-- was added.  And so the principle purpose of this file is to test
-- that IS NULL phrases are correctly optimized.  But you can never
-- have too many tests, so some other tests are thrown in as well.
--
-- $Id: where4.test,v 1.6 2007/12/10 05:03:48 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

-- Build some test data
--
test:do_execsql_test(
    "where4-1.0",
    [[
        -- Tarantool. As far as rowid was replaced w/ PK - no NULLs allowed anymore.
        -- Comment those lines.
            CREATE TABLE t1(w TEXT, x TEXT, y TEXT, primary key (w,x,y));
            INSERT INTO t1 VALUES('1','2','3');
            INSERT INTO t1 VALUES('a','b','c');
            INSERT INTO t1 VALUES('78','79','7a');
            SELECT COUNT(*) FROM t1;
    ]], {
        -- <where4-1.0>
        3
        -- </where4-1.0>
    })

-- Do an SQL statement.  Append the search count to the end of the result.
-- local function count(sql)
--     sql_search_count = 0
--     return test:execsql sql
-- end

-- # Verify that queries use an index.  We are using the special variable
-- # "sql_search_count" which tallys the number of executions of MoveTo
-- # and Next operators in the VDBE.  By verifing that the search count is
-- # small we can be assured that indices are being used properly.
-- #
-- do_test where4-1.1 {
--   count {SELECT rowid FROM t1 WHERE w IS NULL}
-- } {7 2}
-- do_test where4-1.1b {
--   unset -nocomplain null
--   count {SELECT rowid FROM t1 WHERE w IS $null}
-- } {7 2}
-- do_test where4-1.2 {
--   count {SELECT rowid FROM t1 WHERE +w IS NULL}
-- } {7 6}
-- do_test where4-1.3 {
--   count {SELECT rowid FROM t1 WHERE w=1 AND x IS NULL}
-- } {2 2}
-- do_test where4-1.4 {
--   count {SELECT rowid FROM t1 WHERE w=1 AND +x IS NULL}
-- } {2 3}
-- do_test where4-1.5 {
--   count {SELECT rowid FROM t1 WHERE w=1 AND x>0}
-- } {1 2}
-- do_test where4-1.6 {
--   count {SELECT rowid FROM t1 WHERE w=1 AND x<9}
-- } {1 2}
-- do_test where4-1.7 {
--   count {SELECT rowid FROM t1 WHERE w=1 AND x IS NULL AND y=3}
-- } {2 2}
-- do_test where4-1.8 {
--   count {SELECT rowid FROM t1 WHERE w=1 AND x IS NULL AND y>2}
-- } {2 2}
-- do_test where4-1.9 {
--   count {SELECT rowid FROM t1 WHERE w='a' AND x IS NULL AND y='c'}
-- } {4 2}
-- do_test where4-1.10 {
--   count {SELECT rowid FROM t1 WHERE w=x'78' AND x IS NULL}
-- } {6 2}
-- do_test where4-1.11 {
--   count {SELECT rowid FROM t1 WHERE w=x'78' AND x IS NULL AND y=123}
-- } {1}
-- do_test where4-1.12 {
--   count {SELECT rowid FROM t1 WHERE w=x'78' AND x IS NULL AND y=x'7A'}
-- } {6 2}
-- do_test where4-1.13 {
--   count {SELECT rowid FROM t1 WHERE w IS NULL AND x IS NULL}
-- } {7 2}
-- do_test where4-1.14 {
--   count {SELECT rowid FROM t1 WHERE w IS NULL AND x IS NULL AND y IS NULL}
-- } {7 2}
-- do_test where4-1.15 {
--   count {SELECT rowid FROM t1 WHERE w IS NULL AND x IS NULL AND y<0}
-- } {1}
-- do_test where4-1.16 {
--   count {SELECT rowid FROM t1 WHERE w IS NULL AND x IS NULL AND y>=0}
-- } {1}
-- do_test where4-2.1 {
--   execsql {SELECT rowid FROM t1 ORDER BY w, x, y}
-- } {7 2 1 4 3 6 5}
-- do_test where4-2.2 {
--   execsql {SELECT rowid FROM t1 ORDER BY w DESC, x, y}
-- } {6 5 4 3 2 1 7}
-- do_test where4-2.3 {
--   execsql {SELECT rowid FROM t1 ORDER BY w, x DESC, y}
-- } {7 1 2 3 4 5 6}
-- Ticket #2177
--
-- Suppose you have a left join where the right table of the left
-- join (the one that can be NULL) has an index on two columns.
-- The first indexed column is used in the ON clause of the join.
-- The second indexed column is used in the WHERE clause with an IS NULL
-- constraint.  It is not allowed to use the IS NULL optimization to
-- optimize the query because the second column might be NULL because
-- the right table did not match - something the index does not know
-- about.
--
-- Tarantool. As far as NULLs are prohibited for PKs (was UNIQUE + rowid) - block 4-3.* completely
-- do_test where4-3.1 {
--   execsql {
--     CREATE TABLE t2(a INT primary key);
--     INSERT INTO t2 VALUES(1);
--     INSERT INTO t2 VALUES(2);
--     INSERT INTO t2 VALUES(3);
--     CREATE TABLE t3(x INT,y INT, primary key("x", 'y')); -- Goofy syntax allowed
--     INSERT INTO t3 VALUES(1,11);
--     INSERT INTO t3 VALUES(2,NULL);
--     SELECT * FROM t2 LEFT JOIN t3 ON a=x WHERE +y IS NULL;
--   }
-- } {2 2 {} 3 {} {}}
-- do_test where4-3.2 {
--   execsql {
--     SELECT * FROM t2 LEFT JOIN t3 ON a=x WHERE y IS NULL;
--   }
-- } {2 2 {} 3 {} {}}
-- do_test where4-3.3 {
--   execsql {
--     SELECT * FROM t2 LEFT JOIN t3 ON a=x WHERE NULL is y;
--   }
-- } {2 2 {} 3 {} {}}
-- do_test where4-3.4 {
--   unset -nocomplain null
--   execsql {
--     SELECT * FROM t2 LEFT JOIN t3 ON a=x WHERE y IS $null;
--   }
-- } {2 2 {} 3 {} {}}
-- Ticket #2189.  Probably the same bug as #2177.
--
test:do_execsql_test(
    "where4-4.1",
    [[
        CREATE TABLE test(col1 TEXT PRIMARY KEY);
        INSERT INTO test(col1) values('a');
        INSERT INTO test(col1) values('b');
        INSERT INTO test(col1) values('c');
        CREATE TABLE test2(col1 TEXT PRIMARY KEY);
        INSERT INTO test2(col1) values('a');
        INSERT INTO test2(col1) values('b');
        INSERT INTO test2(col1) values('c');
        SELECT * FROM test t1 LEFT OUTER JOIN test2 t2 ON t1.col1 = t2.col1
          WHERE +t2.col1 IS NULL;
    ]], {
        -- <where4-4.1>

        -- </where4-4.1>
    })

test:do_execsql_test(
    "where4-4.2",
    [[
        SELECT * FROM test t1 LEFT OUTER JOIN test2 t2 ON t1.col1 = t2.col1
          WHERE t2.col1 IS NULL;
    ]], {
        -- <where4-4.2>

        -- </where4-4.2>
    })

test:do_execsql_test(
    "where4-4.3",
    [[
        SELECT * FROM test t1 LEFT OUTER JOIN test2 t2 ON t1.col1 = t2.col1
          WHERE +t1.col1 IS NULL;
    ]], {
        -- <where4-4.3>

        -- </where4-4.3>
    })

test:do_execsql_test(
    "where4-4.4",
    [[
        SELECT * FROM test t1 LEFT OUTER JOIN test2 t2 ON t1.col1 = t2.col1
          WHERE t1.col1 IS NULL;
    ]], {
        -- <where4-4.4>

        -- </where4-4.4>
    })

-- Ticket #2273.  Problems with IN operators and NULLs.
--
-- X(203, "X!cmd", [=[["ifcapable","subquery","\ndo_test where4-5.1 {\n  execsql {\n    -- Allow the 'x' syntax for backwards compatibility\n    CREATE TABLE t4(x INT,y INT,z INT,PRIMARY KEY('x' ASC, \"y\" ASC));\n  } } {}\n# execsql {\n#   SELECT *\n#     FROM t2 LEFT JOIN t4 b1\n#             LEFT JOIN t4 b2 ON b2.x=b1.x AND b2.y IN (b1.y);\n# }\n# ","1 {} {} {} {} {} {} 2 {} {} {} {} {} {} 3 {} {} {} {} {} {}"]]=])
test:do_execsql_test(
    "where4-5.1",
    [[
        CREATE TABLE t2(a INT primary key);
        INSERT INTO t2 VALUES(1);
        INSERT INTO t2 VALUES(2);
        INSERT INTO t2 VALUES(3);

        -- Allow the 'x' syntax for backwards compatibility
        CREATE TABLE t4(x INT,y INT,z INT,PRIMARY KEY(x, y));

        SELECT *
          FROM t2 LEFT JOIN t4 b1
                  LEFT JOIN t4 b2 ON b2.x=b1.x AND b2.y IN (b1.y);
    ]], {
        -- <where4-5.2>
        1, '', '', '', '', '', '', 2, '', '', '', '', '', '', 3, '', '', '', '', '', ''
        -- </where4-5.2>
    })


-- SELECT rowid FROM t4 WHERE x IN (1,9,2,5) AND y IN (1,3,NULL,2) AND z!=13;
test:do_execsql_test(
    "where4-5.2",
    [[
        INSERT INTO t4 VALUES(1,1,11);
        INSERT INTO t4 VALUES(1,2,12);
        INSERT INTO t4 VALUES(1,3,13);
        INSERT INTO t4 VALUES(2,2,22);
    ]], {
        -- <where4-5.2>

        -- </where4-5.2>
    })

-- do_test where4-5.3 {
--   execsql {
--     SELECT rowid FROM t4 WHERE x IN (1,9,NULL,2) AND y IN (1,3,2) AND z!=13;
--   }
-- } {1 2 4}
-- do_test where4-6.1 {
--   execsql {
--     CREATE TABLE t5(a INT,b INT,c INT,d INT,e INT,f INT,UNIQUE INT (a,b INT,c INT,d INT,e INT,f INT));
--     INSERT INTO t5 VALUES(1,1,1,1,1,11111);
--     INSERT INTO t5 VALUES(2,2,2,2,2,22222);
--     INSERT INTO t5 VALUES(1,2,3,4,5,12345);
--     INSERT INTO t5 VALUES(2,3,4,5,6,23456);
--   }
--   execsql {
--     SELECT rowid FROM t5
--      WHERE a IN (1,9,2) AND b=2 AND c IN (1,2,3,4) AND d>0
--   }
-- } {3 2}
-- do_test where4-6.2 {
--   execsql {
--     SELECT rowid FROM t5
--      WHERE a IN (1,NULL,2) AND b=2 AND c IN (1,2,3,4) AND d>0
--   }
-- } {3 2}
test:do_test(
    "where4-7.1",
    function()
        test:execsql [[
            CREATE TABLE t6(y INT,z INT,PRIMARY KEY(y,z));
        ]]
        return test:execsql [[
            SELECT * FROM t6 WHERE y=NULL AND z IN ('hello');
        ]]
    end, {
        -- <where4-7.1>

        -- </where4-7.1>
    })

-- integrity_check {where4-99.0}
-- do_test where4-7.1 {
--   execsql {
--     BEGIN;
--     CREATE TABLE t8(a INT primary key, b INT, c INT, d INT);
--     CREATE INDEX t8_i ON t8(a, b, c);
--     CREATE TABLE t7(i INT primary key);
--     INSERT INTO t7 VALUES(1);
--     INSERT INTO t7 SELECT i*2 FROM t7;
--     INSERT INTO t7 SELECT i*2 FROM t7;
--     INSERT INTO t7 SELECT i*2 FROM t7;
--     INSERT INTO t7 SELECT i*2 FROM t7;
--     INSERT INTO t7 SELECT i*2 FROM t7;
--     INSERT INTO t7 SELECT i*2 FROM t7;
--     COMMIT;
--   }
-- } {}
-- # At one point the sub-select inside the aggregate sum() function in the
-- # following query was leaking a couple of stack entries. This query
-- # runs the SELECT in a loop enough times that an assert() fails. Or rather,
-- # did fail before the bug was fixed.
-- #
-- do_test where4-7.2 {
--   execsql {
--     SELECT sum((
--       SELECT d FROM t8 WHERE a = i AND b = i AND c < NULL
--     )) FROM t7;
--   }
-- } {{}}
-- }; #ifcapable subquery
-- #-------------------------------------------------------------------------
-- # Verify that "IS ?" with a NULL bound to the variable also functions
-- # correctly.
-- unset -nocomplain null
-- do_execsql_test 8.1 {
--   CREATE TABLE u9(a INT UNIQUE, b INT);
--   INSERT INTO u9 VALUES(NULL, 1);
--   INSERT INTO u9 VALUES(NULL, 2);
-- }
-- do_execsql_test 8.2 { SELECT * FROM u9 WHERE a IS NULL  } {{} 1 {} 2}
-- do_execsql_test 8.2 { SELECT * FROM u9 WHERE a IS $null } {{} 1 {} 2}
test:finish_test()

