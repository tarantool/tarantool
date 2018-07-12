#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(61)

--!./tcltestrunner.lua
-- 2008 September 1
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
-- $Id: in4.test,v 1.4 2009/06/05 17:09:12 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "in4-1.1",
    [[
        CREATE TABLE t1(a TEXT , b  INT PRIMARY KEY);
        CREATE INDEX i1 ON t1(a);
    ]], {
        -- <in4-1.1>
        
        -- </in4-1.1>
    })

test:do_execsql_test(
    "in4-1.2",
    [[
        SELECT * FROM t1 WHERE a IN ('aaa', 'bbb', 'ccc');
    ]], {
        -- <in4-1.2>
        
        -- </in4-1.2>
    })

test:do_execsql_test(
    "in4-1.3",
    [[
        INSERT INTO t1 VALUES('aaa', 1);
        INSERT INTO t1 VALUES('ddd', 2);
        INSERT INTO t1 VALUES('ccc', 3);
        INSERT INTO t1 VALUES('eee', 4);
        SELECT b FROM t1 WHERE a IN ('aaa', 'bbb', 'ccc');
    ]], {
        -- <in4-1.3>
        1, 3
        -- </in4-1.3>
    })

-- do_test in4-1.4 {
--   execsql {
--     SELECT a FROM t1 WHERE rowid IN (1, 3);
--   }
-- } {aaa ccc}
-- do_test in4-1.5 {
--   execsql {
--     SELECT a FROM t1 WHERE rowid IN ();
--   }
-- } {}
test:do_execsql_test(
    "in4-1.6",
    [[
        SELECT a FROM t1 WHERE a IN ('ddd');
    ]], {
        -- <in4-1.6>
        "ddd"
        -- </in4-1.6>
    })

test:do_execsql_test(
    "in4-2.1",
    [[
        CREATE TABLE t2(a INTEGER PRIMARY KEY, b TEXT);
        INSERT INTO t2 VALUES(-1, '-one');
        INSERT INTO t2 VALUES(0, 'zero');
        INSERT INTO t2 VALUES(1, 'one');
        INSERT INTO t2 VALUES(2, 'two');
        INSERT INTO t2 VALUES(3, 'three');
    ]], {
        -- <in4-2.1>
        
        -- </in4-2.1>
    })

test:do_execsql_test(
    "in4-2.2",
    [[
        SELECT b FROM t2 WHERE a IN (0, 2) 
    ]], {
        -- <in4-2.2>
        "zero", "two"
        -- </in4-2.2>
    })

test:do_execsql_test(
    "in4-2.3",
    [[
        SELECT b FROM t2 WHERE a IN (2, 0) 
    ]], {
        -- <in4-2.3>
        "zero", "two"
        -- </in4-2.3>
    })

test:do_execsql_test(
    "in4-2.4",
    [[
        SELECT b FROM t2 WHERE a IN (2, -1) 
    ]], {
        -- <in4-2.4>
        "-one", "two"
        -- </in4-2.4>
    })

test:do_execsql_test(
    "in4-2.5",
    [[
        SELECT b FROM t2 WHERE a IN (NULL, 3) 
    ]], {
        -- <in4-2.5>
        "three"
        -- </in4-2.5>
    })

test:do_execsql_test(
    "in4-2.6",
    [[
        SELECT b FROM t2 WHERE a IN (1.0, 2.1) 
    ]], {
        -- <in4-2.6>
        "one"
        -- </in4-2.6>
    })

test:do_execsql_test(
    "in4-2.7",
    [[
        SELECT b FROM t2 WHERE a IN ('1', '2') 
    ]], {
        -- <in4-2.7>
        "one", "two"
        -- </in4-2.7>
    })

test:do_execsql_test(
    "in4-2.8",
    [[
        SELECT b FROM t2 WHERE a IN ('', '0.0.0', '2') 
    ]], {
        -- <in4-2.8>
        "two"
        -- </in4-2.8>
    })

-- The following block of tests test expressions of the form:
--
--    <expr> IN ()
--
-- i.e. IN expressions with a literal empty set. 
-- 
-- This has led to crashes on more than one occasion. Test case in4-3.2 
-- was added in reponse to a bug reported on the mailing list on 11/7/2008.
-- See also tickets #3602 and #185.
--
test:do_execsql_test(
    "in4-3.1",
    [[
        DROP TABLE IF EXISTS t1;
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t1(pk  INT primary key, x INT , id INT );
        CREATE TABLE t2(pk  INT primary key, x INT , id INT );
        INSERT INTO t1 VALUES(1, NULL, NULL);
        INSERT INTO t1 VALUES(2, 0, NULL);
        INSERT INTO t1 VALUES(3, 1, 3);
        INSERT INTO t1 VALUES(4, 2, 4);
        INSERT INTO t1 VALUES(5, 3, 5);
        INSERT INTO t1 VALUES(6, 4, 6);
        INSERT INTO t2 VALUES(1, 0, NULL);
        INSERT INTO t2 VALUES(2, 4, 1);
        INSERT INTO t2 VALUES(3, NULL, 1);
        INSERT INTO t2 VALUES(4, NULL, NULL);
    ]], {
        -- <in4-3.1>
        
        -- </in4-3.1>
    })

test:do_execsql_test(
    "in4-3.2",
    [[
        SELECT x FROM t1 WHERE id IN () AND x IN (SELECT x FROM t2 WHERE id=1)
    ]], {
        -- <in4-3.2>
        
        -- </in4-3.2>
    })

test:do_test(
    "in4-3.3",
    function()
        test:execsql [[
            CREATE TABLE t3(x  INT PRIMARY KEY, y INT , z INT );
            CREATE INDEX t3i1 ON t3(x, y);
            INSERT INTO t3 VALUES(1, 1, 1);
            INSERT INTO t3 VALUES(10, 10, 10);
        ]]
        return test:execsql " SELECT * FROM t3 WHERE x IN () "
    end, {
        -- <in4-3.3>
        
        -- </in4-3.3>
    })

test:do_execsql_test(
    "in4-3.4",
    [[
        SELECT * FROM t3 WHERE x = 10 AND y IN () 
    ]], {
        -- <in4-3.4>
        
        -- </in4-3.4>
    })

test:do_execsql_test(
    "in4-3.5",
    [[
        SELECT * FROM t3 WHERE x IN () AND y = 10 
    ]], {
        -- <in4-3.5>
        
        -- </in4-3.5>
    })

test:do_execsql_test(
    "in4-3.6",
    [[
        SELECT * FROM t3 WHERE x IN () OR x = 10 
    ]], {
        -- <in4-3.6>
        10, 10, 10
        -- </in4-3.6>
    })

test:do_execsql_test(
    "in4-3.7",
    [[
        SELECT * FROM t3 WHERE y IN () 
    ]], {
        -- <in4-3.7>
        
        -- </in4-3.7>
    })

test:do_execsql_test(
    "in4-3.8",
    [[
        SELECT x IN() AS a FROM t3 WHERE a 
    ]], {
        -- <in4-3.8>
        
        -- </in4-3.8>
    })

test:do_execsql_test(
    "in4-3.9",
    [[
        SELECT x IN() AS a FROM t3 WHERE NOT a 
    ]], {
        -- <in4-3.9>
        0, 0
        -- </in4-3.9>
    })

test:do_execsql_test(
    "in4-3.10",
    [[
        SELECT * FROM t3 WHERE oid IN () 
    ]], {
        -- <in4-3.10>
        
        -- </in4-3.10>
    })

test:do_execsql_test(
    "in4-3.11",
    [[
        SELECT * FROM t3 WHERE x IN (1, 2) OR y IN ()
    ]], {
        -- <in4-3.11>
        1, 1, 1
        -- </in4-3.11>
    })

test:do_execsql_test(
    "in4-3.12",
    [[
        SELECT * FROM t3 WHERE x IN (1, 2) AND y IN ()
    ]], {
        -- <in4-3.12>
        
        -- </in4-3.12>
    })

-- Tests for "... IN (?)" and "... NOT IN (?)".  In other words, tests
-- for when the RHS of IN is a single expression.  This should work the
-- same as the == and <> operators.
--
test:do_execsql_test(
    "in4-3.21",
    [[
        SELECT * FROM t3 WHERE x=10 AND y IN (10);
    ]], {
        -- <in4-3.21>
        10, 10, 10
        -- </in4-3.21>
    })

test:do_execsql_test(
    "in4-3.22",
    [[
        SELECT * FROM t3 WHERE x IN (10) AND y=10;
    ]], {
        -- <in4-3.22>
        10, 10, 10
        -- </in4-3.22>
    })

test:do_execsql_test(
    "in4-3.23",
    [[
        SELECT * FROM t3 WHERE x IN (10) AND y IN (10);
    ]], {
        -- <in4-3.23>
        10, 10, 10
        -- </in4-3.23>
    })

test:do_execsql_test(
    "in4-3.24",
    [[
        SELECT * FROM t3 WHERE x=1 AND y NOT IN (10);
    ]], {
        -- <in4-3.24>
        1, 1, 1
        -- </in4-3.24>
    })

test:do_execsql_test(
    "in4-3.25",
    [[
        SELECT * FROM t3 WHERE x  NOT IN (10) AND y=1;
    ]], {
        -- <in4-3.25>
        1, 1, 1
        -- </in4-3.25>
    })

test:do_execsql_test(
    "in4-3.26",
    [[
        SELECT * FROM t3 WHERE x NOT IN (10) AND y NOT IN (10);
    ]], {
        -- <in4-3.26>
        1, 1, 1
        -- </in4-3.26>
    })

-- The query planner recognizes that "x IN (?)" only generates a
-- single match and can use this information to optimize-out ORDER BY
-- clauses.
--
test:do_execsql_test(
    "in4-3.31",
    [[
        DROP INDEX t3i1 ON t3;
        CREATE UNIQUE INDEX t3xy ON t3(x,y);

        SELECT *, '|' FROM t3 A, t3 B
         WHERE A.x=10 AND A.y IN (10)
           AND B.x=1 AND B.y IN (1);
    ]], {
        -- <in4-3.31>
        10, 10, 10, 1, 1, 1, "|"
        -- </in4-3.31>
    })

test:do_execsql_test(
    "in4-3.32",
    [[
        EXPLAIN QUERY PLAN
        SELECT *, '|' FROM t3 A, t3 B
         WHERE A.x=10 AND A.y IN (10)
           AND B.x=1 AND B.y IN (1);
    ]], {
        -- <in4-3.32>
        "~/B-TREE/"
        -- </in4-3.32>
    })

-- No separate sorting pass
test:do_execsql_test(
    "in4-3.33",
    [[
        SELECT *, '|' FROM t3 A, t3 B
         WHERE A.x IN (10) AND A.y=10
           AND B.x IN (1) AND B.y=1;
    ]], {
        -- <in4-3.33>
        10, 10, 10, 1, 1, 1, "|"
        -- </in4-3.33>
    })

test:do_execsql_test(
    "in4-3.34",
    [[
        EXPLAIN QUERY PLAN
        SELECT *, '|' FROM t3 A, t3 B
         WHERE A.x IN (10) AND A.y=10
           AND B.x IN (1) AND B.y=1;
    ]], {
        -- <in4-3.34>
        "~/B-TREE/"
        -- </in4-3.34>
    })

-- No separate sorting pass
-- An expression of the form "x IN (?,?)" creates an ephemeral table to
-- hold the list of values on the RHS.  But "x IN (?)" does not create
-- an ephemeral table.
--
test:do_execsql_test(
    "in4-3.41",
    [[
        SELECT * FROM t3 WHERE x IN (10,11);
    ]], {
        -- <in4-3.41>
        10, 10, 10
        -- </in4-3.41>
    })

test:do_execsql_test(
    "in4-3.42",
    [[
        EXPLAIN
        SELECT * FROM t3 WHERE x IN (10,11);
    ]], {
        -- <in4-3.42>
        "/OpenTEphemeral/"
        -- </in4-3.42>
    })

test:do_execsql_test(
    "in4-3.43",
    [[
        SELECT * FROM t3 WHERE x IN (10);
    ]], {
        -- <in4-3.43>
        10, 10, 10
        -- </in4-3.43>
    })

test:do_execsql_test(
    "in4-3.44",
    [[
        EXPLAIN
        SELECT * FROM t3 WHERE x IN (10);
    ]], {
        -- <in4-3.44>
        "~/OpenTEphemeral/"
        -- </in4-3.44>
    })

test:do_execsql_test(
    "in4-3.45",
    [[
        SELECT * FROM t3 WHERE x NOT IN (10,11,99999);
    ]], {
        -- <in4-3.45>
        1, 1, 1
        -- </in4-3.45>
    })

test:do_execsql_test(
    "in4-3.46",
    [[
        EXPLAIN
        SELECT * FROM t3 WHERE x NOT IN (10,11,99999);
    ]], {
        -- <in4-3.46>
        "/OpenTEphemeral/"
        -- </in4-3.46>
    })

test:do_execsql_test(
    "in4-3.47",
    [[
        SELECT * FROM t3 WHERE x NOT IN (10);
    ]], {
        -- <in4-3.47>
        1, 1, 1
        -- </in4-3.47>
    })

test:do_execsql_test(
    "in4-3.48",
    [[
        EXPLAIN
        SELECT * FROM t3 WHERE x NOT IN (10);
    ]], {
        -- <in4-3.48>
        "~/OpenTEphemeral/"
        -- </in4-3.48>
    })

-- MUST_WORK_TEST
-- Make sure that when "x IN (?)" is converted into "x==?" that collating
-- sequence and affinity computations do not get messed up.
--
test:do_execsql_test(
    "in4-4.1",
    [[
        CREATE TABLE t4a(a TEXT, b TEXT COLLATE "unicode_ci", c  INT PRIMARY KEY);
        INSERT INTO t4a VALUES('ABC','abc',1);
        INSERT INTO t4a VALUES('def','xyz',2);
        INSERT INTO t4a VALUES('ghi','ghi',3);
        SELECT c FROM t4a WHERE a=b ORDER BY c;
    ]], {
        -- <in4-4.1>
        3
        -- </in4-4.1>
    })

test:do_execsql_test(
    "in4-4.2",
    [[
        SELECT c FROM t4a WHERE b=a ORDER BY c;
    ]], {
        -- <in4-4.2>
        1, 3
        -- </in4-4.2>
    })

test:do_execsql_test(
    "in4-4.3",
    [[
        SELECT c FROM t4a WHERE (a||'')=b ORDER BY c;
    ]], {
        -- <in4-4.3>
        1, 3
        -- </in4-4.3>
    })

test:do_execsql_test(
    "in4-4.4",
    [[
        SELECT c FROM t4a WHERE (a||'')=(b||'') ORDER BY c;
    ]], {
        -- <in4-4.4>
        3
        -- </in4-4.4>
    })

test:do_execsql_test(
    "in4-4.5",
    [[
        SELECT c FROM t4a WHERE a IN (b) ORDER BY c;
    ]], {
        -- <in4-4.5>
        3
        -- </in4-4.5>
    })

test:do_execsql_test(
    "in4-4.6",
    [[
        SELECT c FROM t4a WHERE (a||'') IN (b) ORDER BY c;
    ]], {
        -- <in4-4.6>
        3
        -- </in4-4.6>
    })

test:do_execsql_test(
    "in4-4.11",
    [[
        CREATE TABLE t4b(a TEXT, b NUMERIC, c  INT PRIMARY KEY);
        INSERT INTO t4b VALUES('1.0',1,4);
        SELECT c FROM t4b WHERE a=b;
    ]], {
        -- <in4-4.11>
        4
        -- </in4-4.11>
    })

test:do_execsql_test(
    "in4-4.12",
    [[
        SELECT c FROM t4b WHERE b=a;
    ]], {
        -- <in4-4.12>
        4
        -- </in4-4.12>
    })

test:do_execsql_test(
    "in4-4.13",
    [[
        SELECT c FROM t4b WHERE +a=b;
    ]], {
        -- <in4-4.13>
        4
        -- </in4-4.13>
    })

test:do_execsql_test(
    "in4-4.14",
    [[
        SELECT c FROM t4b WHERE a=+b;
    ]], {
        -- <in4-4.14>
        4
        -- </in4-4.14>
    })

test:do_execsql_test(
    "in4-4.15",
    [[
        SELECT c FROM t4b WHERE +b=a;
    ]], {
        -- <in4-4.15>
        4
        -- </in4-4.15>
    })

test:do_execsql_test(
    "in4-4.16",
    [[
        SELECT c FROM t4b WHERE b=+a;
    ]], {
        -- <in4-4.16>
        4
        -- </in4-4.16>
    })

test:do_execsql_test(
    "in4-4.17",
    [[
        SELECT c FROM t4b WHERE a IN (b);
    ]], {
        -- <in4-4.17>
        4
        -- </in4-4.17>
    })

test:do_execsql_test(
    "in4-4.18",
    [[
        SELECT c FROM t4b WHERE b IN (a);
    ]], {
        -- <in4-4.18>
        4
        -- </in4-4.18>
    })

test:do_execsql_test(
    "in4-4.19",
    [[
        SELECT c FROM t4b WHERE +b IN (a);
    ]], {
        -- <in4-4.19>
        
        -- </in4-4.19>
    })

-- MUST_WORK_TEST
-- Tarantool: TBI: Need to support collations. Depends on #2121
-- test:do_execsql_test(
--     "in4-5.1",
--     [[
--         CREATE TABLE t5(c INTEGER PRIMARY KEY, d TEXT COLLATE nocase);
--         INSERT INTO t5 VALUES(17, 'fuzz');
--         SELECT 1 FROM t5 WHERE 'fuzz' IN (d);  -- match
--         SELECT 2 FROM t5 WHERE 'FUZZ' IN (d);  -- no match
--         SELECT 3 FROM t5 WHERE d IN ('fuzz');  -- match
--         SELECT 4 FROM t5 WHERE d IN ('FUZZ');  -- match
--     ]], {
--         -- <in4-5.1>
--         1, 3, 4
--         -- </in4-5.1>
--     })

-- An expression of the form "x IN (y)" can be used as "x=y" by the
-- query planner when computing transitive constraints or to run the
-- query using an index on y.
--
test:do_execsql_test(
    "in4-6.1",
    [[
        CREATE TABLE t6a(a INTEGER PRIMARY KEY, b INT );
        INSERT INTO t6a VALUES(1,2),(3,4),(5,6);
        CREATE TABLE t6b(c INTEGER PRIMARY KEY, d INT );
        INSERT INTO t6b VALUES(4,44),(5,55),(6,66);

        SELECT * FROM t6a, t6b WHERE a=3 AND b IN (c);
    ]], {
        -- <in4-6.1>
        3, 4, 4, 44
        -- </in4-6.1>
    })

test:do_execsql_test(
    "in4-6.1-eqp",
    [[
        EXPLAIN QUERY PLAN
        SELECT * FROM t6a, t6b WHERE a=3 AND b IN (c);
    ]], {
        -- <in4-6.1-eqp>
        "~/SCAN/"
        -- </in4-6.1-eqp>
    })

test:do_execsql_test(
    "in4-6.2",
    [[
        SELECT * FROM t6a, t6b WHERE a=3 AND c IN (b);
    ]], {
        -- <in4-6.2>
        3, 4, 4, 44
        -- </in4-6.2>
    })

test:do_execsql_test(
    "in4-6.2-eqp",
    [[
        EXPLAIN QUERY PLAN
        SELECT * FROM t6a, t6b WHERE a=3 AND c IN (b);
    ]], {
        -- <in4-6.2-eqp>
        "~/SCAN/"
        -- </in4-6.2-eqp>
    })

test:finish_test()

