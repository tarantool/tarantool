#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(7)

--!./tcltestrunner.lua
-- 2012 August 23
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library.
--
-- This file implements tests for processing aggregate queries with
-- subqueries in which the subqueries hold the aggregate functions
-- or in which the subqueries are themselves aggregate queries
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "aggnested-1.1",
    [[
        CREATE TABLE t1(a1 STRING PRIMARY KEY);
        INSERT INTO t1 VALUES('1'), ('2'), ('3');
        CREATE TABLE t2(b1 STRING PRIMARY KEY);
        INSERT INTO t2 VALUES('4'), ('5');
        SELECT (SELECT GROUP_CONCAT(a1,'x') FROM t2 LIMIT 1) FROM t1;
    ]],
    {
        -- <aggnested-1.1>
        "1x2x3"
        -- </aggnested-1.1>
    })

test:do_execsql_test(
    "aggnested-1.2",
    [[
        SELECT
         (SELECT GROUP_CONCAT(a1,'x') || '-' || GROUP_CONCAT(b1,'y') FROM t2)
        FROM t1;
    ]],
    {
        -- <aggnested-1.2>
        "1x2x3-4y5"
        -- </aggnested-1.2>
    })

test:do_execsql_test("aggnested-1.3",
    [[
        SELECT (SELECT GROUP_CONCAT(b1,a1) FROM t2) FROM t1;
    ]],
    {
        -- <aggnested-1.3>
        "415", "425", "435"
        -- </aggnested-1.3>
    })

test:do_execsql_test("aggnested-1.4",
    [[
        SELECT (SELECT GROUP_CONCAT(a1,b1) FROM t2) FROM t1;
    ]],
    {
        -- <aggnested-1.4>
        "151", "252", "353"
        -- </aggnested-1.4>
    })

-- # This test case is a copy of the one in
-- # http://www.mail-archive.com/sql-users@sql.org/msg70787.html
-- #
-- do_test aggnested-2.0 {
--   db2 eval {
--     CREATE TABLE t1 (A1 INTEGER NOT NULL,A2 INTEGER NOT NULL,A3 INTEGER NOT
--     NULL,A4 INTEGER NOT NULL,PRIMARY KEY(A1));
--     REPLACE INTO t1 VALUES(1,11,111,1111);
--     REPLACE INTO t1 VALUES(2,22,222,2222);
--     REPLACE INTO t1 VALUES(3,33,333,3333);
--     CREATE TABLE t2 (B1 INTEGER NOT NULL,B2 INTEGER NOT NULL,B3 INTEGER NOT
--     NULL,B4 INTEGER NOT NULL,PRIMARY KEY(B1));
--     REPLACE INTO t2 VALUES(1,88,888,8888);
--     REPLACE INTO t2 VALUES(2,99,999,9999);
--     SELECT (SELECT GROUP_CONCAT(CASE WHEN a1=1 THEN'A' ELSE 'B' END) FROM t2),
--             t1.*
--     FROM t1;
--   }
-- } {A,B,B 3 33 333 3333}
-- db2 close
--#################### Test cases for ticket [bfbf38e5e9956ac69f] ############
--
-- This first test case is the original problem report:

-- Tarantool: should work after autoincrement is supported (gh-2263)
-- test:do_execsql_test("aggnested-3.0",
--     [[
--         CREATE TABLE AAA (
--           aaa_id       INTEGER PRIMARY KEY AUTOINCREMENT
--         );
--         CREATE TABLE RRR (
--           rrr_id      INTEGER     PRIMARY KEY AUTOINCREMENT,
--           rrr_date    INTEGER     NOT NULL,
--           rrr_aaa     INTEGER
--         );
--         CREATE TABLE TTT (
--           ttt_id      INTEGER PRIMARY KEY AUTOINCREMENT,
--           target_aaa  INTEGER NOT NULL,
--           source_aaa  INTEGER NOT NULL
--         );
--         insert into AAA (aaa_id) values (2);
--         insert into TTT (ttt_id, target_aaa, source_aaa)
--         values (4469, 2, 2);
--         insert into TTT (ttt_id, target_aaa, source_aaa)
--         values (4476, 2, 1);
--         insert into RRR (rrr_id, rrr_date, rrr_aaa)
--         values (0, 0, NULL);
--         insert into RRR (rrr_id, rrr_date, rrr_aaa)
--         values (2, 4312, 2);
--         SELECT i.aaa_id,
--           (SELECT sum(CASE WHEN (t.source_aaa == i.aaa_id) THEN 1 ELSE 0 END)
--              FROM TTT t
--           ) AS segfault
--         FROM
--          (SELECT curr.rrr_aaa as aaa_id
--             FROM RRR curr
--               -- you also can comment out the next line
--               -- it causes segfault to happen after one row is outputted
--               INNER JOIN AAA a ON (curr.rrr_aaa = aaa_id)
--               LEFT JOIN RRR r ON (r.rrr_id <> 0 AND r.rrr_date < curr.rrr_date)
--            GROUP BY curr.rrr_id
--           HAVING r.rrr_date IS NULL
--         ) i;
--     ]],
--     {
--         -- <aggnested-3.0>
--         2, 1
--         -- </aggnested-3.0>
--     })

-- MUST_WORK_TEST
-- # Further variants of the test case, as found in the ticket
-- #
-- do_test aggnested-3.1 {
--   db eval {
--     DROP TABLE IF EXISTS t1;
--     DROP TABLE IF EXISTS t2;
--     CREATE TABLE t1 (
--       id1 INTEGER PRIMARY KEY AUTOINCREMENT,
--       value1 INTEGER
--     );
--     INSERT INTO t1 VALUES(4469,2),(4476,1);
--     CREATE TABLE t2 (
--       id2 INTEGER PRIMARY KEY AUTOINCREMENT,
--       value2 INTEGER
--     );
--     INSERT INTO t2 VALUES(0,1),(2,2);
--     SELECT
--      (SELECT sum(value2==xyz) FROM t2)
--     FROM
--      (SELECT curr.value1 as xyz
--         FROM t1 AS curr LEFT JOIN t1 AS other
--        GROUP BY curr.id1);
--   }
-- } {1 1}
test:do_execsql_test("aggnested-3.2",
    [[
        DROP TABLE IF EXISTS t1;
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t1 (
          id1 INTEGER,
          value1 INTEGER PRIMARY KEY,
          x1 INTEGER
        );
        INSERT INTO t1 VALUES(4469,2,98),(4469,1,99),(4469,3,97);
        CREATE TABLE t2 (
          value2 INTEGER PRIMARY KEY
        );
        INSERT INTO t2 VALUES(1);
        SELECT
         (SELECT SUM(xyz) FROM t2 where xyz = value2)
        FROM
         (SELECT value1 as xyz, MAX(x1) AS pqr
            FROM t1
           GROUP BY id1);
    ]],
    {
        -- <aggnested-3.2>
        1
        -- </aggnested-3.2>
    })

test:do_execsql_test("aggnested-3.2-2",
    [[
        DROP TABLE IF EXISTS t1;
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t1 (
          id1 INTEGER,
          value1 INTEGER PRIMARY KEY,
          x1 INTEGER
        );
        INSERT INTO t1 VALUES(4469,2,98),(4469,1,99),(4469,3,97);
        CREATE TABLE t2 (
          value2 INTEGER PRIMARY KEY
        );
        INSERT INTO t2 VALUES(1);
        SELECT
         (SELECT SUM(xyz) FROM t2 where xyz <> value2)
        FROM
         (SELECT value1 as xyz, MAX(x1) AS pqr
            FROM t1
           GROUP BY id1);
    ]],
    {
        -- <aggnested-3.2>
        ""
        -- </aggnested-3.2>
    })

test:do_execsql_test("aggnested-3.3",
    [[
        DROP TABLE IF EXISTS t1;
        DROP TABLE IF EXISTS t2;
        CREATE TABLE t1(id1 INT, value1 INT PRIMARY KEY);
        INSERT INTO t1 VALUES(4469,2),(4469,1);
        CREATE TABLE t2 (value2 INT PRIMARY KEY);
        INSERT INTO t2 VALUES(1);
        SELECT (SELECT SUM(value1) FROM t2 where value1=value2), MAX(value1)
          FROM t1
         GROUP BY id1;
    ]],
    {
        -- <aggnested-3.3>
        "", 2
        -- </aggnested-3.3>
    })

-- # A batch of queries all doing approximately the same operation involving
-- # two nested aggregate queries.
-- #
-- do_test aggnested-3.11 {
--   db eval {
--     DROP TABLE IF EXISTS t1;
--     DROP TABLE IF EXISTS t2;
--     CREATE TABLE t1(id1, value1);
--     INSERT INTO t1 VALUES(4469,12),(4469,11),(4470,34);
--     CREATE INDEX t1id1 ON t1(id1);
--     CREATE TABLE t2 (value2);
--     INSERT INTO t2 VALUES(12),(34),(34);
--     INSERT INTO t2 SELECT value2 FROM t2;
--     SELECT max(value1), (SELECT count(*) FROM t2 WHERE value2=max(value1))
--       FROM t1
--      GROUP BY id1;
--   }
-- } {12 2 34 4}
-- do_test aggnested-3.12 {
--   db eval {
--     SELECT max(value1), (SELECT count(*) FROM t2 WHERE value2=value1)
--       FROM t1
--      GROUP BY id1;
--   }
-- } {12 2 34 4}
-- do_test aggnested-3.13 {
--   db eval {
--     SELECT value1, (SELECT sum(value2=value1) FROM t2)
--       FROM t1;
--   }
-- } {12 2 11 0 34 4}
-- do_test aggnested-3.14 {
--   db eval {
--     SELECT value1, (SELECT sum(value2=value1) FROM t2)
--       FROM t1
--      WHERE value1 IN (SELECT max(value1) FROM t1 GROUP BY id1);
--   }
-- } {12 2 34 4}
-- do_test aggnested-3.15 {
--   # FIXME:  If case 3.16 works, then this case really ought to work too...
--   catchsql {
--     SELECT max(value1), (SELECT sum(value2=max(value1)) FROM t2)
--       FROM t1
--      GROUP BY id1;
--   }
-- } {1 {misuse of aggregate function max()}}
-- do_test aggnested-3.16 {
--   db eval {
--     SELECT max(value1), (SELECT sum(value2=value1) FROM t2)
--       FROM t1
--      GROUP BY id1;
--   }
-- } {12 2 34 4}
test:finish_test()

