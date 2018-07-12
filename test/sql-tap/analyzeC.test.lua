#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(20)

testprefix = "analyzeC"


--!./tcltestrunner.lua
-- 2014-07-22
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
-- This file contains automated tests used to verify that the text terms
-- at the end of "_sql_stat1".stat are processed correctly.
--
--  (1) "unordered" means that the index cannot be used for ORDER BY
--      or for range queries
--
--  (2) "sz=NNN" sets the relative size of the index entries
--
--  (3) All other fields are silently ignored
--
-- Baseline case.  Range queries work OK.  Indexes can be used for
-- ORDER BY.

test:do_execsql_test(
    1.0,
    [[
        DROP TABLE IF EXISTS t1;
        CREATE TABLE t1(a  INT PRIMARY KEY, b INT , c INT , d INT );
        INSERT INTO t1(a,b,c,d) VALUES(1,1,2,3),(2,7,8,9),(3,4,5,6),(4,10,11,12),(5,4,8,12),(6,1,11,111);
        CREATE INDEX t1b ON t1(b);
        CREATE INDEX t1c ON t1(c);
        ANALYZE;
        DELETE FROM "_sql_stat1";
        INSERT INTO "_sql_stat1"("tbl","idx","stat") VALUES('t1','t1b','12345 2'),('t1','t1c','12345 4');
        ANALYZE;
        SELECT b,c,d, '#' FROM t1 WHERE b BETWEEN 3 AND 8 ORDER BY d;
    ]], {
        -- <1.0>
        4, 5, 6, "#", 7, 8, 9, "#", 4, 8, 12, "#"
        -- </1.0>
    })

test:do_execsql_test(
    1.1,
    [[
        EXPLAIN QUERY PLAN SELECT b, c, d, '#' FROM t1 WHERE b BETWEEN 3 AND 8 ORDER BY d;
    ]], {
        -- <1.1>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1B (B>? AND B<?)",
        0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"
        -- </1.1>
    })

test:do_execsql_test(
    1.2,
    [[
        SELECT d FROM t1 ORDER BY b;
    ]], {
        -- <1.2>
        3, 111, 6, 12, 9, 12
        -- </1.2>
    })

test:do_execsql_test(
    1.3,
    [[
        EXPLAIN QUERY PLAN SELECT d FROM t1 ORDER BY b;
    ]], {
        -- <1.3>
        0, 0, 0, "SCAN TABLE T1 USING COVERING INDEX T1B"
        -- </1.3>
    })

-- Now mark the t1a index as "unordered".  Range queries and ORDER BY no
-- longer use the index, but equality queries do.
--
test:do_execsql_test(
    2.0,
    [[
        UPDATE "_sql_stat1" SET "stat"='12345 2 unordered' WHERE "idx"='t1b';
        ANALYZE;
        SELECT b, c, d, '#' FROM t1 WHERE b BETWEEN 3 AND 8 ORDER BY d;
    ]], {
        -- <2.0>
        4, 5, 6, "#", 7, 8, 9, "#", 4, 8, 12, "#"
        -- </2.0>
    })

test:do_execsql_test(
    2.1,
    [[
        EXPLAIN QUERY PLAN SELECT b, c, d, '#' FROM t1 WHERE b BETWEEN 3 AND 8 ORDER BY d;
    ]], {
        -- <2.1>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1B (B>? AND B<?)",
        0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"
        -- </2.1>
    })

test:do_execsql_test(
    2.2,
    [[
        SELECT d FROM t1 ORDER BY b;
    ]], {
        -- <2.2>
        3, 111, 6, 12, 9, 12
        -- </2.2>
    })

test:do_execsql_test(
    2.3,
    [[
        EXPLAIN QUERY PLAN SELECT d FROM t1 ORDER BY b;
    ]], {
        -- <2.3>
        0, 0, 0, "SCAN TABLE T1 USING COVERING INDEX T1B"
        -- </2.3>
    })

-- Ignore extraneous text parameters in the "_sql_stat1".stat field.
--
test:do_execsql_test(
    3.0,
    [[
        UPDATE "_sql_stat1" SET "stat"='12345 2 whatever=5 unordered xyzzy=11' WHERE "idx"='t1b';
        ANALYZE;
        SELECT b, c, d, '#' FROM t1 WHERE b BETWEEN 3 AND 8 ORDER BY d;
    ]], {
        -- <3.0>
        4, 5, 6, "#", 7, 8, 9, "#", 4, 8, 12, "#"
        -- </3.0>
    })

test:do_execsql_test(
    3.1,
    [[
        EXPLAIN QUERY PLAN SELECT b, c, d, '#' FROM t1 WHERE b BETWEEN 3 AND 8 ORDER BY d;
    ]], {
        -- <3.1>
        0, 0, 0, "SEARCH TABLE T1 USING COVERING INDEX T1B (B>? AND B<?)",
        0, 0, 0, "USE TEMP B-TREE FOR ORDER BY"
        -- </3.1>
    })

test:do_execsql_test(
    3.2,
    [[
        SELECT d FROM t1 ORDER BY b;
    ]], {
        -- <3.2>
        3, 111, 6, 12, 9, 12
        -- </3.2>
    })

test:do_execsql_test(
    3.3,
    [[
        EXPLAIN QUERY PLAN SELECT d FROM t1 ORDER BY b;
    ]], {
        -- <3.3>
        0, 0, 0, "SCAN TABLE T1 USING COVERING INDEX T1B"
        -- </3.3>
    })

-- The sz=NNN parameter determines which index to scan
--
test:do_execsql_test(
    4.0,
    [[
        DROP INDEX t1b ON t1;
        CREATE INDEX t1bc ON t1(b,c);
        CREATE INDEX t1db ON t1(d,b);
        DELETE FROM "_sql_stat1";
        INSERT INTO "_sql_stat1"("tbl","idx","stat") VALUES('t1','t1bc','12345 3 2 sz=10'),('t1','t1db','12345 3 2 sz=20');
        ANALYZE;
        SELECT count(b) FROM t1;
    ]], {
        -- <4.0>
        6
        -- </4.0>
    })

test:do_execsql_test(
    4.1,
    [[
        EXPLAIN QUERY PLAN SELECT count(b) FROM t1;
    ]], {
        -- <4.1>
        0, 0, 0, "SCAN TABLE T1"
        -- </4.1>
    })

test:do_execsql_test(
    4.2,
    [[
        DELETE FROM "_sql_stat1";
        INSERT INTO "_sql_stat1"("tbl","idx","stat") VALUES('t1','t1bc','12345 3 2 sz=20'),('t1','t1db','12345 3 2 sz=10');
        ANALYZE;
        SELECT count(b) FROM t1;
    ]], {
        -- <4.2>
        6
        -- </4.2>
    })

test:do_execsql_test(
    4.3,
    [[
        EXPLAIN QUERY PLAN SELECT count(b) FROM t1;
    ]], {
        -- <4.3>
        0, 0, 0, "SCAN TABLE T1"
        -- </4.3>
    })

-- The sz=NNN parameter works even if there is other extraneous text
-- in the sqlite_stat1.stat column.
--
test:do_execsql_test(
    5.0,
    [[
        DELETE FROM "_sql_stat1";
        INSERT INTO "_sql_stat1"("tbl","idx","stat")
          VALUES('t1','t1bc','12345 3 2 x=5 sz=10 y=10'),
                ('t1','t1db','12345 3 2 whatever sz=20 junk');
        ANALYZE;
        SELECT count(b) FROM t1;
    ]], {
        -- <5.0>
        6
        -- </5.0>
    })

test:do_execsql_test(
    5.1,
    [[
        EXPLAIN QUERY PLAN
        SELECT count(b) FROM t1;
    ]], {
        -- <5.1>
        0, 0, 0, "SCAN TABLE T1"
        -- </5.1>
    })

test:do_execsql_test(
    5.2,
    [[
        DELETE FROM "_sql_stat1";
        INSERT INTO "_sql_stat1"("tbl","idx","stat") VALUES('t1','t1db','12345 3 2 x=5 sz=10 y=10'), ('t1','t1bc','12345 3 2 whatever sz=20 junk');
        ANALYZE;
        SELECT count(b) FROM t1;
    ]], {
        -- <5.2>
        6
        -- </5.2>
    })

test:do_execsql_test(
    5.3,
    [[
        EXPLAIN QUERY PLAN SELECT count(b) FROM t1;
    ]], {
        -- <5.3>
        0, 0, 0, "SCAN TABLE T1"
        -- </5.3>
    })


test:finish_test()
