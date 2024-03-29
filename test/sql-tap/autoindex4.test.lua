#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(8)

--!./tcltestrunner.lua
-- 2014-10-24
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
---------------------------------------------------------------------------
--
-- This file implements regression tests for sql library.  The
-- focus of this script is testing automatic index creation logic,
-- and specifically creation of automatic partial indexes.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "autoindex4-1.0",
    [[
        CREATE TABLE t1(a INT,b TEXT, primary key(a,b));
        INSERT INTO t1 VALUES(123,'abc'),(234,'def'),(234,'ghi'),(345,'jkl');
        CREATE TABLE t2(x INT,y TEXT, primary key(x,y));
        INSERT INTO t2 VALUES(987,'zyx'),(654,'wvu'),(987,'rqp');

        SELECT *, '|' FROM t1, t2 WHERE a=234 AND x=987 ORDER BY +b;
    ]], {
        -- <autoindex4-1.0>
        234, "def", 987, "rqp", "|", 234, "def", 987, "zyx", "|", 234, "ghi", 987, "rqp", "|", 234, "ghi", 987, "zyx", "|"
        -- </autoindex4-1.0>
    })

test:do_execsql_test(
    "autoindex4-1.1",
    [[
        SELECT *, '|' FROM t1, t2 WHERE a=234 AND x=555;
    ]], {
        -- <autoindex4-1.1>

        -- </autoindex4-1.1>
    })

test:do_execsql_test(
    "autoindex4-1.2",
    [[
        SELECT *, '|' FROM t1 LEFT JOIN t2 ON a=234 AND x=555;
    ]], {
        -- <autoindex4-1.2>
        123, "abc", "", "", "|", 234, "def", "", "", "|", 234, "ghi", "", "", "|", 345, "jkl", "", "", "|"
        -- </autoindex4-1.2>
    })

test:do_execsql_test(
    "autoindex4-1.3",
    [[
        SELECT *, '|' FROM t1 LEFT JOIN t2 ON x=555 WHERE a=234;
    ]], {
        -- <autoindex4-1.3>
        234, "def", "", "", "|", 234, "ghi", "", "", "|"
        -- </autoindex4-1.3>
    })

test:do_execsql_test(
    "autoindex4-1.4",
    [[
        SELECT *, '|' FROM t1 LEFT JOIN t2 WHERE a=234 AND x=555;
    ]], {
        -- <autoindex4-1.4>

        -- </autoindex4-1.4>
    })

test:do_execsql_test(
    "autoindex4-2.0",
    [[
        CREATE TABLE t3(i INT PRIMARY KEY, e INT, f INT);
        INSERT INTO t3 VALUES(1, 123,654), (2, 555,444), (3, 234,987);
        SELECT (SELECT COUNT(*) FROM t1, t2 WHERE a=e AND x=f), e, f, '|' FROM t3 ORDER BY i;
    ]], {
        -- <autoindex4-2.0>
        1, 123, 654, "|", 0, 555, 444, "|", 4, 234, 987, "|"
        -- </autoindex4-2.0>
    })

-- do_execsql_test autoindex4-2.0 {
--   CREATE TABLE t3(e INT,f INT);
--   INSERT INTO t3 VALUES(123,654),(555,444),(234,987);
--   SELECT (SELECT count(*) FROM t1, t2 WHERE a=e AND x=f), e, f, '|'
--     FROM t3
--    ORDER BY rowid;
-- } {1 123 654 | 0 555 444 | 4 234 987 |}
-- Ticket [2326c258d02ead33d]
-- Two joins, one with and the other without an ORDER BY clause.
-- The one without ORDER BY correctly returns two rows of result.
-- The one with ORDER BY returns no rows.
--
test:do_execsql_test(
    "autoindex4-3.0",
    [[
        CREATE TABLE A(Name text primary key);
        CREATE TABLE Items(ItemName text primary key, Name text);
        INSERT INTO Items VALUES('Item1','Parent');
        INSERT INTO Items VALUES('Item2','Parent');
        CREATE TABLE B(Name text primary key);

        SELECT Items.ItemName
          FROM Items
            LEFT JOIN A ON (A.Name = Items.ItemName and Items.ItemName = 'dummy')
            LEFT JOIN B ON (B.Name = Items.ItemName)
          WHERE Items.Name = 'Parent'
          ORDER BY Items.ItemName;
    ]], {
        -- <autoindex4-3.0>
        "Item1", "Item2"
        -- </autoindex4-3.0>
    })

test:do_execsql_test(
    "autoindex4-3.1",
    [[
        CREATE INDEX Items_x1 ON Items(ItemName,Name);

        SELECT Items.ItemName
          FROM Items
            LEFT JOIN A ON (A.Name = Items.ItemName and Items.ItemName = 'dummy')
            LEFT JOIN B ON (B.Name = Items.ItemName)
          WHERE Items.Name = 'Parent'
          ORDER BY Items.ItemName;
    ]], {
        -- <autoindex4-3.1>
        "Item1", "Item2"
        -- </autoindex4-3.1>
    })

test:finish_test()

