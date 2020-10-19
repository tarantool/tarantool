#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(36)

--!./tcltestrunner.lua
-- 2012 August 24
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
-- focus of this file is testing that an index may be used as a covering
-- index when there are OR expressions in the WHERE clause. 
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
local testprefix = "whereD"
test:do_execsql_test(
    1.1,
    [[
        CREATE TABLE t(i int PRIMARY key,j int,k TEXT, m INT, n TEXT);
        CREATE INDEX ijk ON t(i,j,k);
        CREATE INDEX jmn ON t(j,m,n);

        INSERT INTO t VALUES(3, 3, 'three', 3, 'tres');
        INSERT INTO t VALUES(2, 2, 'two', 2, 'dos');
        INSERT INTO t VALUES(1, 1, 'one', 1, 'uno');
        INSERT INTO t VALUES(4, 4, 'four', 4, 'cuatro');
    ]])

test:do_execsql_test(
    1.2,
    [[
        SELECT k FROM t WHERE (i=1 AND j=1) OR (i=2 AND j=2);
    ]], {
        -- <1.2>
        "one", "two"
        -- </1.2>
    })

test:do_execsql_test(
    1.3,
    [[
        SELECT k FROM t WHERE (i=1 AND j=1) OR (+i=2 AND j=2);
    ]], {
        -- <1.3>
        "one", "two"
        -- </1.3>
    })

test:do_execsql_test(
    1.4,
    [[
        SELECT n FROM t WHERE (i=1 AND j=1) OR (i=2 AND j=2);
    ]], {
        -- <1.4>
        "uno", "dos"
        -- </1.4>
    })

test:do_execsql_test(
    1.5,
    [[
        SELECT k, n FROM t WHERE (i=1 AND j=1) OR (i=2 AND j=2);
    ]], {
        -- <1.5>
        "one", "uno", "two", "dos"
        -- </1.5>
    })

test:do_execsql_test(
    1.6,
    [[
        SELECT k FROM t WHERE (i=1 AND j=1) OR (i=2 AND j=2) OR (i=3 AND j=3);
    ]], {
        -- <1.6>
        "one", "two", "three"
        -- </1.6>
    })

test:do_execsql_test(
    1.7,
    [[
        SELECT n FROM t WHERE (i=1 AND j=1) OR (i=2 AND j=2) OR (i=3 AND j=3);
    ]], {
        -- <1.7>
        "uno", "dos", "tres"
        -- </1.7>
    })

test:do_execsql_test(
    1.8,
    [[
        SELECT k FROM t WHERE (i=1 AND j=1) OR (j=2 AND m=2);
    ]], {
        -- <1.8>
        "one", "two"
        -- </1.8>
    })

test:do_execsql_test(
    1.9,
    [[
        SELECT k FROM t WHERE (i=1 AND j=1) OR (i=2 AND j=2) OR (j=3 AND m=3);
    ]], {
        -- <1.9>
        "one", "two", "three"
        -- </1.9>
    })

test:do_execsql_test(
    1.10,
    [[
        SELECT n FROM t WHERE (i=1 AND j=1) OR (i=2 AND j=2) OR (j=3 AND m=3);
    ]], {
        -- <1.10>
        "uno", "dos", "tres"
        -- </1.10>
    })

test:do_execsql_test(
    1.11,
    [[
        SELECT k FROM t WHERE (i=1 AND j=1) OR (j=2 AND m=2) OR (i=3 AND j=3);
    ]], {
        -- <1.11>
        "one", "two", "three"
        -- </1.11>
    })

test:do_execsql_test(
    1.12,
    [[
        SELECT n FROM t WHERE (i=1 AND j=1) OR (j=2 AND m=2) OR (i=3 AND j=3);
    ]], {
        -- <1.12>
        "uno", "dos", "tres"
        -- </1.12>
    })

test:do_execsql_test(
    1.13,
    [[
        SELECT k FROM t WHERE (j=1 AND m=1) OR (i=2 AND j=2) OR (i=3 AND j=3);
    ]], {
        -- <1.13>
        "one", "two", "three"
        -- </1.13>
    })

test:do_execsql_test(
    1.14,
    [[
        SELECT k FROM t WHERE (i=1 AND j=1) OR (j=2 AND i=2) OR (i=3 AND j=3);
    ]], {
        -- <1.14>
        "one", "two", "three"
        -- </1.14>
    })

test:do_execsql_test(
    1.15,
    [[
        SELECT k FROM t WHERE (i=1 AND j=2) OR (i=2 AND j=1) OR (i=3 AND j=4);
    ]], {
        -- <1.15>
        
        -- </1.15>
    })

test:do_execsql_test(
    1.16,
    [[
        SELECT k FROM t WHERE (i=1 AND (j=1 or j=2)) OR (i=3 AND j=3);
    ]], {
        -- <1.16>
        "one", "three"
        -- </1.16>
    })

test:do_execsql_test(
    2.0,
    [[
        CREATE TABLE t1(a  INT PRIMARY KEY,b INT ,c INT ,d INT );
        CREATE INDEX t1b ON t1(b);
        CREATE INDEX t1c ON t1(c);
        CREATE INDEX t1d ON t1(d);
        CREATE TABLE t2(x  INT PRIMARY KEY,y INT );
        CREATE INDEX t2y ON t2(y);

        INSERT INTO t1 VALUES(1,2,3,4);
        INSERT INTO t1 VALUES(5,6,7,8);
        INSERT INTO t2 VALUES(1,2);
        INSERT INTO t2 VALUES(2,7);
        INSERT INTO t2 VALUES(3,4);
    ]], {
        -- <2.0>
        
        -- </2.0>
    })

test:do_execsql_test(
    2.1,
    [[
        SELECT a, x FROM t1 JOIN t2 ON +y=d OR x=7 ORDER BY a, x;
    ]], {
        -- <2.1>
        1, 3
        -- </2.1>
    })

test:do_execsql_test(
    2.2,
    [[
        SELECT a, x FROM t1 JOIN t2 ON y=d OR x=7 ORDER BY a, x;
    ]], {
        -- <2.2>
        1, 3
        -- </2.2>
    })

-- Similar to [do_execsql_test], except that two elements are appended
-- to the result - the string "search" and the number of times test variable
-- sql_search_count is incremented by running the supplied SQL. e.g.
--
--   do_searchcount_test 1.0 { SELECT * FROM t1 } {x y search 2}
--
local function do_searchcount_test(tn, sql, res)
    test:do_test(
        tn,
        function()
            local sql_search_count = box.stat.sql().sql_search_count
            local r = test:execsql(sql)
            table.insert(r, "search")
            table.insert(r, box.stat.sql().sql_search_count - sql_search_count)
            return r
        end,
        res)
end


test:do_execsql_test(
    3.0,
    [[
        CREATE TABLE t3(a  INT PRIMARY KEY, b TEXT, c TEXT);
        CREATE UNIQUE INDEX i3 ON t3(a, b);
        INSERT INTO t3 VALUES(1, 'one', 'i');
        INSERT INTO t3 VALUES(3, 'three', 'iii');
        INSERT INTO t3 VALUES(6, 'six', 'vi');
        INSERT INTO t3 VALUES(2, 'two', 'ii');
        INSERT INTO t3 VALUES(4, 'four', 'iv');
        INSERT INTO t3 VALUES(5, 'five', 'v');

        CREATE TABLE t4(x  TEXT PRIMARY KEY, y TEXT);
        INSERT INTO t4 VALUES('a', 'one');
        INSERT INTO t4 VALUES('b', 'two');
    ]])

-- since extra columns deleted from pk (#2289), the searchcount decreased because
-- query plan changed a lot
do_searchcount_test("3.1", [[
  SELECT a, b FROM t3 WHERE (a=1 AND b='one') OR (a=2 AND b='two')
    ]],
    {1, "one", 2, "two", "search", 2})
do_searchcount_test("3.2", [[
  SELECT a, c FROM t3 WHERE (a=1 AND b='one') OR (a=2 AND b='two')
    ]],
    {1, "i", 2, "ii", "search", 2})
do_searchcount_test("3.4.1", [[
  SELECT y FROM t4 WHERE x='a'
    ]],
    {"one", "search", 1})
do_searchcount_test("3.4.2", [[
  SELECT a, b FROM t3 WHERE
        (a=1 AND b=(SELECT y FROM t4 WHERE x='a'))
     OR (a=2 AND b='two')
    ]],
    {1, "one", 2, "two", "search", 3})
do_searchcount_test("3.4.3", [[
  SELECT a, b FROM t3 WHERE
        (a=2 AND b='two')
     OR (a=1 AND b=(SELECT y FROM t4 WHERE x='a'))
    ]],
    {2, "two", 1, "one", "search", 3})
do_searchcount_test("3.4.4", [[
  SELECT a, b FROM t3 WHERE
        (a=2 AND b=(SELECT y FROM t4 WHERE x='b'))
     OR (a=1 AND b=(SELECT y FROM t4 WHERE x='a'))
    ]],
    {2, "two", 1, "one", "search", 4})

-- do_searchcount_test 3.5.1 {
--   SELECT a, b FROM t3 WHERE (a=1 AND b='one') OR rowid=4
-- } {1 one 2 two search 3}
-- do_searchcount_test 3.5.2 {
--   SELECT a, c FROM t3 WHERE (a=1 AND b='one') OR rowid=4
-- } {1 i 2 ii search 3}
-- Ticket [d02e1406a58ea02d] (2012-10-04)
-- LEFT JOIN with an OR in the ON clause causes segfault
--
test:do_test(
    4.1,
    function()
        return test:execsql [[
            CREATE TABLE t41(a  INT PRIMARY KEY,b INT ,c INT );
            INSERT INTO t41 VALUES(1,2,3), (4,5,6);
            CREATE TABLE t42(d  INT PRIMARY KEY,e INT ,f INT );
            INSERT INTO t42 VALUES(3,6,9), (4,8,12);
            SELECT * FROM t41 AS x LEFT JOIN t42 AS y ON (y.d=x.c) OR (y.e=x.b);
        ]]
    end, {
        -- <4.1>
        1, 2, 3, 3, 6, 9, 4, 5, 6, "", "", ""
        -- </4.1>
    })

test:do_test(
    4.2,
    function()
        return test:execsql [[
            CREATE INDEX t42e ON t42(e);
            SELECT * FROM t41 AS x LEFT JOIN t42 AS y ON (y.d=x.c) OR (y.e=x.b);
        ]]
    end, {
        -- <4.2>
        1, 2, 3, 3, 6, 9, 4, 5, 6, "", "", ""
        -- </4.2>
    })

test:do_test(
    4.3,
    function()
        return test:execsql [[
            SELECT * FROM t41 AS x LEFT JOIN t42 AS y ON (y.d=x.c) OR (y.d=x.b);
        ]]
    end, {
        -- <4.3>
        1, 2, 3, 3, 6, 9, 4, 5, 6, "", "", ""
        -- </4.3>
    })

-- Ticket [bc1aea7b725f276177]
-- Incorrect result on LEFT JOIN with OR constraints and an ORDER BY clause.
--
test:do_execsql_test(
    4.4,
    [[
        CREATE TABLE t44(a INTEGER PRIMARY KEY, b INTEGER);
        INSERT INTO t44 VALUES(1,2);
        INSERT INTO t44 VALUES(3,4);
        SELECT *
          FROM t44 AS x
             LEFT JOIN (SELECT a AS c, b AS d FROM t44) AS y ON a=c
         WHERE d=4 OR d IS NULL;
    ]], {
        -- <4.4>
        3, 4, 3, 4
        -- </4.4>
    })

test:do_execsql_test(
    4.5,
    [[
        SELECT *
          FROM t44 AS x
             LEFT JOIN (SELECT a AS c, b AS d FROM t44) AS y ON a=c
         WHERE d=4 OR d IS NULL
         ORDER BY a;
    ]], {
        -- <4.5>
        3, 4, 3, 4
        -- </4.5>
    })

test:do_execsql_test(
    4.6,
    [[
        CREATE TABLE t46(c INTEGER PRIMARY KEY, d INTEGER);
        INSERT INTO t46 SELECT a, b FROM t44;
        SELECT * FROM t44 LEFT JOIN t46 ON a=c
         WHERE d=4 OR d IS NULL;
    ]], {
        -- <4.6>
        3, 4, 3, 4
        -- </4.6>
    })

test:do_execsql_test(
    4.7,
    [[
        SELECT * FROM t44 LEFT JOIN t46 ON a=c
         WHERE d=4 OR d IS NULL
         ORDER BY a;
    ]], {
        -- <4.7>
        3, 4, 3, 4
        -- </4.7>
    })

-- Verify fix of a bug reported on the mailing list by Peter Reid
--
test:do_execsql_test(
    5.1,
    [[
        DROP TABLE IF EXISTS t;
        CREATE TABLE t(c0  INT PRIMARY key,c1 INT ,c2 INT ,c3 INT ,c4 INT ,c5 INT ,c6 INT ,c7 INT ,c8 INT ,c9 INT ,c10 INT ,c11 INT ,c12 INT ,c13 INT ,c14 INT ,c15 INT ,c16 INT ,c17 INT );
        CREATE INDEX tc1 ON t(c1);
        CREATE INDEX tc2 ON t(c2);
        CREATE INDEX tc3 ON t(c3);
        CREATE INDEX tc4 ON t(c4);
        CREATE INDEX tc5 ON t(c5);
        CREATE INDEX tc6 ON t(c6);
        CREATE INDEX tc7 ON t(c7);
        CREATE INDEX tc8 ON t(c8);
        CREATE INDEX tc9 ON t(c9);
        CREATE INDEX tc10 ON t(c10);
        CREATE INDEX tc11 ON t(c11);
        CREATE INDEX tc12 ON t(c12);
        CREATE INDEX tc13 ON t(c13);
        CREATE INDEX tc14 ON t(c14);
        CREATE INDEX tc15 ON t(c15);
        CREATE INDEX tc16 ON t(c16);
        CREATE INDEX tc17 ON t(c17);

        INSERT INTO t(c0, c16) VALUES (1,1);

        SELECT * FROM t WHERE
          c0=1 or  c1=1 or  c2=1 or  c3=1 or
          c4=1 or  c5=1 or  c6=1 or  c7=1 or
          c8=1 or  c9=1 or c10=1 or c11=1 or
          c12=1 or c13=1 or c14=1 or c15=1 or
          c16=1 or c17=1;
    ]], {
        -- <5.1>
        1, "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 1, ""
        -- </5.1>
    })

test:do_execsql_test(
    5.2,
    [[
        DELETE FROM t;
        INSERT INTO t(c0,c17) VALUES(1,1);
        SELECT * FROM t WHERE
          c0=1 or  c1=1 or  c2=1 or  c3=1 or
          c4=1 or  c5=1 or  c6=1 or  c7=1 or
          c8=1 or  c9=1 or c10=1 or c11=1 or
          c12=1 or c13=1 or c14=1 or c15=1 or
          c16=1 or c17=1;
    ]], {
        -- <5.2>
        1, "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 1
        -- </5.2>
    })

test:do_execsql_test(
    5.3,
    [[
        DELETE FROM t;
        INSERT INTO t(c0,c15) VALUES(1,1);
        SELECT * FROM t WHERE
          c0=1 or  c1=1 or  c2=1 or  c3=1 or
          c4=1 or  c5=1 or  c6=1 or  c7=1 or
          c8=1 or  c9=1 or c10=1 or c11=1 or
          c12=1 or c13=1 or c14=1 or c15=1 or
          c16=1 or c17=1;
    ]], {
        -- <5.3>
        1, "", "", "", "", "", "", "", "", "", "", "", "", "", "", 1, "", ""
        -- </5.3>
    })

test:finish_test()

