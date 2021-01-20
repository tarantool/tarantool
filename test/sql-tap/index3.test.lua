#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(2)

--!./tcltestrunner.lua
-- 2005-02-14
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
-- focus of this file is testing the CREATE INDEX statement.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Ticket #1115.  Make sure that when a UNIQUE index is created on a
-- non-unique column (or columns) that it fails and that it leaves no
-- residue behind.
--
test:do_execsql_test(
    "index3-1.1",
    [[
        CREATE TABLE t1(id  INT primary key, a INT );
        INSERT INTO t1 VALUES(1, 1);
        INSERT INTO t1 VALUES(2, 1);
        SELECT a FROM t1;
    ]], {
        -- <index3-1.1>
        1, 1
        -- </index3-1.1>
    })

-- MUST_WORK_TEST
-- do_test index3-1.2 {
--   catchsql {
--     BEGIN;
--     CREATE UNIQUE INDEX i1 ON t1(a);
--   }
-- } {1 {UNIQUE constraint failed: t1.a}}
-- do_test index3-1.3 {
--   catchsql COMMIT;
-- } {0 {}}
-- integrity_check index3-1.4
-- Backwards compatibility test:
--
-- Verify that CREATE INDEX statements that use strings instead of
-- identifiers for the the column names continue to work correctly.
-- This is undocumented behavior retained for backwards compatiblity.
--
test:do_execsql_test(
    "index3-2.1",
    [[
        DROP TABLE t1;
        CREATE TABLE t1(a INT , b TEXT , c INT , d INT , e INT ,
                        PRIMARY KEY(a), UNIQUE(b COLLATE "unicode_ci" DESC));
        CREATE INDEX t1c ON t1(c);
        CREATE INDEX t1d ON t1(d);
        WITH RECURSIVE c(x) AS (VALUES(1) UNION SELECT x+1 FROM c WHERE x<30)
          INSERT INTO t1(a,b,c,d,e)
            SELECT x, printf('ab%03xxy',x), x, x, x FROM c;
    ]], {
        -- <index3-2.1>

        -- </index3-2.1>
    })

-- commented until #2121
-- test:do_execsql_test(
--     "index3-2.2",
--     [[
--         SELECT a FROM t1 WHERE b='ab005xy' COLLATE nocase;
--     ]], {
--         -- <index3-2.2>
--         5
--         -- </index3-2.2>
--     })

-- -- MUST_WORK_TEST
-- test:do_execsql_test(
--     "index3-2.2eqp",
--     [[
--         EXPLAIN QUERY PLAN
--         SELECT a FROM t1 WHERE b='ab005xy' COLLATE nocase;
--     ]], {
--         -- <index3-2.2eqp>
--         "/SEARCH TABLE t1/"
--         -- </index3-2.2eqp>
--     })

-- # This test corrupts the database file so it must be the last test
-- # in the series.
-- #
-- do_test index3-99.1 {
--   execsql {
--     UPDATE sql_master SET sql='nonsense' WHERE name='t1d'
--   }
--   db close
--   catch { sql db test.db }
--   catchsql { DROP INDEX t1c }
-- } {1 {malformed database schema (t1d)}}
test:finish_test()

