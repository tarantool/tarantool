#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(18)

--!./tcltestrunner.lua
-- 2005 January 13
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
-- focus of this file is testing corner cases of the INSERT statement.
--
-- $Id: insert3.test,v 1.9 2009/04/23 14:58:40 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

-- All the tests in this file require trigger support
--
-- Create a table and a corresponding insert trigger.  Do a self-insert
-- into the table.
--
test:do_execsql_test(
    "insert3-1.0",
    [[
            CREATE TABLE t1(rowid INTEGER PRIMARY KEY AUTOINCREMENT, a INT ,b INT );
            CREATE TABLE log(rowid INTEGER PRIMARY KEY AUTOINCREMENT, x  INT UNIQUE, y INT );
            CREATE TRIGGER r1 AFTER INSERT ON t1 BEGIN
              UPDATE log SET y=y+1 WHERE x=new.a;
              INSERT OR IGNORE INTO log(x, y) VALUES(new.a, 1);
            END;
            INSERT INTO t1(a, b) VALUES('hello','world');
            INSERT INTO t1(a, b) VALUES(5,10);
            SELECT x,y FROM log ORDER BY x;
    ]], {
        -- <insert3-1.0>
        5, 1, "hello", 1
        -- </insert3-1.0>
})

test:do_execsql_test(
    "insert3-1.1",
    [[
            INSERT INTO t1(a, b) SELECT a, b+10 FROM t1;
            SELECT x, y FROM log ORDER BY x;
    ]], {
        -- <insert3-1.1>
        5, 2, "hello", 2
        -- </insert3-1.1>
})

test:do_execsql_test(
    "insert3-1.2",
    [[
            CREATE TABLE log2(rowid INTEGER PRIMARY KEY AUTOINCREMENT, x  INT UNIQUE,y INT );
            CREATE TRIGGER r2 BEFORE INSERT ON t1 BEGIN
              UPDATE log2 SET y=y+1 WHERE x=new.b;
              INSERT OR IGNORE INTO log2(x, y) VALUES(new.b,1);
            END;
            INSERT INTO t1(a, b) VALUES(453,'hi');
            SELECT x,y FROM log ORDER BY x;
    ]], {
        -- <insert3-1.2>
        5, 2, 453, 1, "hello", 2
        -- </insert3-1.2>
})

test:do_execsql_test(
    "insert3-1.3",
    [[
            SELECT x,y FROM log2 ORDER BY x;
    ]], {
        -- <insert3-1.3>
        "hi", 1
        -- </insert3-1.3>
})

test:do_execsql_test(
    "insert3-1.4.1",
    [[
            INSERT INTO t1(a,b) SELECT a,b FROM t1;
            SELECT 'a:', x, y FROM log UNION ALL
                SELECT 'b:', x, y FROM log2 ORDER BY x;
    ]], {
        -- <insert3-1.4.1>
        "a:", 5, 4, "b:", 10, 2, "b:", 20, 1, "a:", 453, 2, "a:", "hello", 4, "b:", "hi", 2, "b:", "world", 1
        -- </insert3-1.4.1>
})

test:do_execsql_test(
    "insert3-1.4.2",
    [[
            SELECT 'a:', x, y FROM log UNION ALL
                SELECT 'b:', x, y FROM log2 ORDER BY x, y;
    ]], {
        -- <insert3-1.4.2>
        "a:", 5, 4, "b:", 10, 2, "b:", 20, 1, "a:", 453, 2, "a:", "hello", 4, "b:", "hi", 2, "b:", "world", 1
        -- </insert3-1.4.2>
})

test:do_execsql_test(
    "insert3-1.5",
    [[
            INSERT INTO t1(a) VALUES('xyz');
            SELECT x,y FROM log ORDER BY x;
    ]], {
        -- <insert3-1.5>
        5, 4, 453, 2, "hello", 4, "xyz", 1
        -- </insert3-1.5>
})



test:do_execsql_test(
    "insert3-2.1",
    [[
            CREATE TABLE t2(
              a INTEGER PRIMARY KEY AUTOINCREMENT,
              b  INT DEFAULT 'b',
              c  INT DEFAULT 'c'
            );
            CREATE TABLE t2dup(rowid INTEGER PRIMARY KEY AUTOINCREMENT, a INT ,b INT ,c INT );
            CREATE TRIGGER t2r1 BEFORE INSERT ON t2 BEGIN
              INSERT INTO t2dup(a,b,c) VALUES(new.a,new.b,new.c);
            END;
            INSERT INTO t2(a) VALUES(123);
            INSERT INTO t2(b) VALUES(234);
            INSERT INTO t2(c) VALUES(345);
            SELECT * FROM t2dup;
    ]], {
        -- <insert3-2.1>
        1, 123, "b", "c", 2, -1, 234, "c", 3, -1, "b", 345
        -- </insert3-2.1>
})

test:do_execsql_test(
    "insert3-2.2",
    [[
            DELETE FROM t2dup;
            INSERT INTO t2(a) SELECT 1 FROM t1 LIMIT 1;
            INSERT INTO t2(b) SELECT 987 FROM t1 LIMIT 1;
            INSERT INTO t2(c) SELECT 876 FROM t1 LIMIT 1;
            SELECT * FROM t2dup;
    ]], {
        -- <insert3-2.2>
        4, 1, "b", "c", 5, -1, 987, "c", 6, -1, "b", 876
        -- </insert3-2.2>
})

-- Test for proper detection of malformed WHEN clauses on INSERT triggers.
--
test:do_execsql_test(
    "insert3-3.1",
    [[
            CREATE TABLE t3(id INTEGER PRIMARY KEY AUTOINCREMENT, a INT ,b INT ,c INT );
            CREATE TRIGGER t3r1 BEFORE INSERT on t3 WHEN nosuchcol BEGIN
              SELECT 'illegal WHEN clause';
            END;
    ]], {
        -- <insert3-3.1>
        -- </insert3-3.1>
})

test:do_catchsql_test(
    "insert3-3.2",
    [[
            INSERT INTO t3 (a,b,c)VALUES(1,2,3)
    ]], {
        -- <insert3-3.2>
        1, "no such column: NOSUCHCOL"
        -- </insert3-3.2>
})

test:do_execsql_test(
    "insert3-3.3",
    [[
            CREATE TABLE t4(id INTEGER PRIMARY KEY AUTOINCREMENT, a INT ,b INT ,c INT );
            CREATE TRIGGER t4r1 AFTER INSERT on t4 WHEN nosuchcol BEGIN
              SELECT 'illegal WHEN clause';
            END;
    ]], {
        -- <insert3-3.3>

        -- </insert3-3.3>
})

test:do_catchsql_test(
    "insert3-3.4",
    [[
            INSERT INTO t4 (a,b,c)VALUES(1,2,3)
    ]], {
        -- <insert3-3.4>
        1, "no such column: NOSUCHCOL"
        -- </insert3-3.4>
})



-- ifcapable {trigger}
-- Tests for the INSERT INTO ... DEFAULT VALUES construct
--
test:do_execsql_test(
    "insert3-3.5",
    [[
            CREATE TABLE t5(
              a INTEGER PRIMARY KEY AUTOINCREMENT,
              b  INT DEFAULT 'xyz'
            );
            INSERT INTO t5 DEFAULT VALUES;
            SELECT * FROM t5;
    ]], {
        -- <insert3-3.5>
        1, "xyz"
        -- </insert3-3.5>
})

test:do_execsql_test(
    "insert3-3.6",
    [[
            INSERT INTO t5 DEFAULT VALUES;
            SELECT * FROM t5;
    ]], {
        -- <insert3-3.6>
        1, "xyz", 2, "xyz"
        -- </insert3-3.6>
})

test:do_execsql_test(
    "insert3-3.7",
    [[
            CREATE TABLE t6(id INTEGER PRIMARY KEY AUTOINCREMENT, x INT ,y  INT DEFAULT 4.3, z  INT DEFAULT x'6869');
            INSERT INTO t6 DEFAULT VALUES;
            SELECT * FROM t6;
    ]], {
        -- <insert3-3.7>
        1, "", 4.3, "hi"
        -- </insert3-3.7>
})

test:execsql("CREATE TABLE t7(id INTEGER PRIMARY KEY AUTOINCREMENT, a INT DEFAULT 1);")

test:do_execsql_test(
	"insert3-4.1",
	[[
		INSERT INTO t7 DEFAULT VALUES;
	]], {
		-- <insert3-4.1>
		-- <insert3-4.1>
})

test:do_execsql_test(
	"insert3-4.1",
	[[
		INSERT INTO t7(a) VALUES(10);
	]], {
		-- <insert3-4.1>
		-- <insert3-4.1>
})

test:drop_all_tables()
---------------------------------------------------------------------------
-- While developing tests for a different feature (savepoint) the following
-- sequence was found to cause an assert() in btree.c to fail. These
-- tests are included to ensure that that bug is fixed.
--
-- Tarantool: comment until randstr() is implemented
--     test:do_test(
--         "insert3-4.1",
--         function()
--             test:execsql([[
--                 CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, a INT , b INT , c INT );
--                 CREATE INDEX i1 ON t1(a, b);
--                 BEGIN;
--                 INSERT INTO t1 (a,b,c)VALUES(randstr(10,400),randstr(10,400),randstr(10,400));
--             ]])
--             r = "randstr(10,400)"
--             -- for _ in X(0, "X!for", [=[["set ii 0","$ii < 10","incr ii"]]=]) do
--             for ii=0,9 do
--                 test:execsql(string.format("INSERT INTO t1 (a, b, c) SELECT %s, %s, %s FROM t1", r, r, r))
--             end
--             return test:execsql(" COMMIT ")
--         end, {
--             -- <insert3-4.1>
--             -- </insert3-4.1>
--         })

--     test:do_execsql_test(
--         "insert3-4.2",
--         [[
--             BEGIN;
--               UPDATE t1 SET a = randstr(10,10) WHERE (id%4)==0;
--               DELETE FROM t1 WHERE id%2;
--               INSERT INTO t1 SELECT randstr(10,400), randstr(10,400), c FROM t1;
--             COMMIT;
--         ]], {
--             -- <insert3-4.2>

--             -- </insert3-4.2>
--         })

test:finish_test()
