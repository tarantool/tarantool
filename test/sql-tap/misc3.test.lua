#!/usr/bin/env tarantool
test = require("sqltester")
local json = require("json")
test:plan(34)

--!./tcltestrunner.lua
-- 2003 December 17
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library.
--
-- This file implements tests for miscellanous features that were
-- left out of other test files.
--
-- $Id: misc3.test,v 1.20 2009/05/06 00:49:01 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Ticket #529.  Make sure an ABORT does not damage the in-memory cache
-- that will be used by subsequent statements in the same transaction.
--
test:do_test(
    "misc3-1.1",
    function()
        test:execsql([[
            CREATE TABLE t1(a PRIMARY KEY,b);
            INSERT INTO t1
              VALUES(1,'a23456789_b23456789_c23456789_d23456789_e23456789_');
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
            INSERT INTO t1 VALUES(2,'x');
            UPDATE t1 SET b=substr(b,1,500);
            CREATE TABLE t2(x PRIMARY KEY,y);
            BEGIN;
        ]])
        test:catchsql("UPDATE t1 SET a=CASE a WHEN 2 THEN 1 ELSE a END, b='y';")
        return test:execsql([[
            COMMIT;
            --PRAGMA integrity_check;
        ]])
    end, {
        -- <misc3-1.1>
        
        -- </misc3-1.1>
    })



test:do_test(
    "misc3-1.2",
    function()
        test:execsql([[
            DROP TABLE t1;
            DROP TABLE t2;
        ]])
        test:execsql([[
            CREATE TABLE t1(a PRIMARY KEY,b);
            INSERT INTO t1
            VALUES(1,'a23456789_b23456789_c23456789_d23456789_e23456789_');
            INSERT INTO t1 SELECT a+1, b||b FROM t1;
            INSERT INTO t1 SELECT a+2, b||b FROM t1;
            INSERT INTO t1 SELECT a+4, b FROM t1;
            INSERT INTO t1 SELECT a+8, b FROM t1;
            INSERT INTO t1 SELECT a+16, b FROM t1;
            INSERT INTO t1 SELECT a+32, b FROM t1;
            INSERT INTO t1 SELECT a+64, b FROM t1;
            BEGIN;
        ]])
        test:catchsql("UPDATE t1 SET a=CASE a WHEN 128 THEN 127 ELSE a END, b='';")
        return test:execsql([[
            INSERT INTO t1 VALUES(200,'hello out there');
            COMMIT;
            --PRAGMA integrity_check;
        ]])
    end, {
        -- <misc3-1.2>
        
        -- </misc3-1.2>
    })



-- Tests of the sqliteAtoF() function in util.c
--
test:do_execsql_test(
    "misc3-2.1",
    [[
        SELECT 2e-25*0.5e25
    ]], {
        -- <misc3-2.1>
        1.0
        -- </misc3-2.1>
    })

test:do_execsql_test(
    "misc3-2.2",
    [[
        SELECT 2.0e-25*000000.500000000000000000000000000000e+00025
    ]], {
        -- <misc3-2.2>
        1.0
        -- </misc3-2.2>
    })

test:do_execsql_test(
    "misc3-2.3",
    [[
        SELECT 000000000002e-0000000025*0.5e25
    ]], {
        -- <misc3-2.3>
        1.0
        -- </misc3-2.3>
    })

test:do_execsql_test(
    "misc3-2.4",
    [[
        SELECT 2e-25*0.5e250
    ]], {
        -- <misc3-2.4>
        1e+225
        -- </misc3-2.4>
    })

test:do_execsql_test(
    "misc3-2.5",
    [[
        SELECT 2.0e-250*0.5e25
    ]], {
        -- <misc3-2.5>
        1e-225
        -- </misc3-2.5>
    })

test:do_execsql_test(
    "misc3-2.6",
    [[
        SELECT '-2.0e-127' * '-0.5e27'
    ]], {
        -- <misc3-2.6>
        1e-100
        -- </misc3-2.6>
    })

test:do_execsql_test(
    "misc3-2.7",
    [[
        SELECT '+2.0e-127' * '-0.5e27'
    ]], {
        -- <misc3-2.7>
        -1e-100
        -- </misc3-2.7>
    })

test:do_execsql_test(
    "misc3-2.8",
    [[
        SELECT 2.0e-27 * '+0.5e+127'
    ]], {
        -- <misc3-2.8>
        1e+100
        -- </misc3-2.8>
    })

test:do_execsql_test(
    "misc3-2.9",
    [[
        SELECT 2.0e-27 * '+0.000005e+132'
    ]], {
        -- <misc3-2.9>
        1e+100
        -- </misc3-2.9>
    })

-- Ticket #522.  Make sure integer overflow is handled properly in
-- indices.
--
--integrity_check misc3-3.1
test:do_execsql_test(
    "misc3-3.2",
    [[
        CREATE TABLE t2(a INT PRIMARY KEY);
    ]], {
        -- <misc3-3.2>
        
        -- </misc3-3.2>
    })

-- integrity_check misc3-3.2.1
test:do_execsql_test(
    "misc3-3.3",
    [[
        INSERT INTO t2 VALUES(2147483648);
    ]], {
        -- <misc3-3.3>
        
        -- </misc3-3.3>
    })

-- integrity_check misc3-3.3.1
test:do_execsql_test(
    "misc3-3.4",
    [[
        INSERT INTO t2 VALUES(-2147483649);
    ]], {
        -- <misc3-3.4>
        
        -- </misc3-3.4>
    })

-- integrity_check misc3-3.4.1
test:do_execsql_test(
    "misc3-3.5",
    [[
        INSERT INTO t2 VALUES(+2147483649);
    ]], {
        -- <misc3-3.5>
        
        -- </misc3-3.5>
    })

-- integrity_check misc3-3.5.1
test:do_execsql_test(
    "misc3-3.6",
    [[
        INSERT INTO t2 VALUES(+2147483647);
        INSERT INTO t2 VALUES(-2147483648);
        INSERT INTO t2 VALUES(-2147483647);
        INSERT INTO t2 VALUES(2147483646);
        SELECT * FROM t2 ORDER BY a;
    ]], {
        -- <misc3-3.6>
        -2147483649, -2147483648, -2147483647, 2147483646, 2147483647, 2147483648, 2147483649
        -- </misc3-3.6>
    })

test:do_execsql_test(
    "misc3-3.7",
    [[
        SELECT * FROM t2 WHERE a>=-2147483648 ORDER BY a;
    ]], {
        -- <misc3-3.7>
        -2147483648, -2147483647, 2147483646, 2147483647, 2147483648, 2147483649
        -- </misc3-3.7>
    })

test:do_execsql_test(
    "misc3-3.8",
    [[
        SELECT * FROM t2 WHERE a>-2147483648 ORDER BY a;
    ]], {
        -- <misc3-3.8>
        -2147483647, 2147483646, 2147483647, 2147483648, 2147483649
        -- </misc3-3.8>
    })

test:do_execsql_test(
    "misc3-3.9",
    [[
        SELECT * FROM t2 WHERE a>-2147483649 ORDER BY a;
    ]], {
        -- <misc3-3.9>
        -2147483648, -2147483647, 2147483646, 2147483647, 2147483648, 2147483649
        -- </misc3-3.9>
    })

test:do_execsql_test(
    "misc3-3.10",
    [[
        SELECT * FROM t2 WHERE a>=0 AND a<2147483649 ORDER BY a DESC;
    ]], {
        -- <misc3-3.10>
        2147483648, 2147483647, 2147483646
        -- </misc3-3.10>
    })

test:do_execsql_test(
    "misc3-3.11",
    [[
        SELECT * FROM t2 WHERE a>=0 AND a<=2147483648 ORDER BY a DESC;
    ]], {
        -- <misc3-3.11>
        2147483648, 2147483647, 2147483646
        -- </misc3-3.11>
    })

test:do_execsql_test(
    "misc3-3.12",
    [[
        SELECT * FROM t2 WHERE a>=0 AND a<2147483648 ORDER BY a DESC;
    ]], {
        -- <misc3-3.12>
        2147483647, 2147483646
        -- </misc3-3.12>
    })

test:do_execsql_test(
    "misc3-3.13",
    [[
        SELECT * FROM t2 WHERE a>=0 AND a<=2147483647 ORDER BY a DESC;
    ]], {
        -- <misc3-3.13>
        2147483647, 2147483646
        -- </misc3-3.13>
    })

test:do_execsql_test(
    "misc3-3.14",
    [[
        SELECT * FROM t2 WHERE a>=0 AND a<2147483647 ORDER BY a DESC;
    ]], {
        -- <misc3-3.14>
        2147483646
        -- </misc3-3.14>
    })

-- Ticket #565.  A stack overflow is occurring when the subquery to the
-- right of an IN operator contains many NULLs
--
test:do_execsql_test(
    "misc3-4.1",
    [[
        CREATE TABLE t3(a INTEGER PRIMARY KEY, b);
        INSERT INTO t3 VALUES(1, 'abc');
        INSERT INTO t3 VALUES(2, 'xyz');
        INSERT INTO t3 VALUES(3, NULL);
        INSERT INTO t3 VALUES(4, NULL);
        INSERT INTO t3 SELECT a+4,b||'d' FROM t3;
        INSERT INTO t3 SELECT a+8,b||'e' FROM t3;
        INSERT INTO t3 SELECT a+16,b||'f' FROM t3;
        INSERT INTO t3 SELECT a+32,b||'g' FROM t3;
        INSERT INTO t3 SELECT a+64,b||'h' FROM t3;
        SELECT count(a), count(b) FROM t3;
    ]], {
        -- <misc3-4.1>
        128, 64
        -- </misc3-4.1>
    })

test:do_execsql_test(
    "misc3-4.2",
    [[
        SELECT count(a) FROM t3 WHERE b IN (SELECT b FROM t3);
    ]], {
        -- <misc3-4.2>
        64
        -- </misc3-4.2>
    })

test:do_execsql_test(
    "misc3-4.3",
    [[
        SELECT count(a) FROM t3 WHERE b IN (SELECT b FROM t3 ORDER BY a+1);
    ]], {
        -- <misc3-4.3>
        64
        -- </misc3-4.3>
    })



-- Ticket #601:  Putting a left join inside "SELECT * FROM (<join-here>)"
-- gives different results that if the outer "SELECT * FROM ..." is omitted.
--
test:do_execsql_test(
    "misc3-5.1",
    [[
        CREATE TABLE x1 (id primary key, b, c);
        INSERT INTO x1 VALUES(1, 'dog',3);
        INSERT INTO x1 VALUES(2, 'cat',1);
        INSERT INTO x1 VALUES(3, 'dog',4);
        CREATE TABLE x2 (c primary key, e);
        INSERT INTO x2 VALUES(1,'one');
        INSERT INTO x2 VALUES(2,'two');
        INSERT INTO x2 VALUES(3,'three');
        INSERT INTO x2 VALUES(4,'four');
        SELECT x2.c AS c, e, b FROM x2 LEFT JOIN
           (SELECT b, max(c)+0 AS c FROM x1 GROUP BY b)
           USING(c);
    ]], {
        -- <misc3-5.1>
        1, "one", "cat", 2, "two", "", 3, "three", "", 4, "four", "dog"
        -- </misc3-5.1>
    })

test:do_execsql_test(
    "misc3-5.2",
    [[
        SELECT * FROM (
          SELECT x2.c AS c, e, b FROM x2 LEFT JOIN
             (SELECT b, max(c)+0 AS c FROM x1 GROUP BY b)
             USING(c)
        );
    ]], {
        -- <misc3-5.2>
        1, "one", "cat", 2, "two", "", 3, "three", "", 4, "four", "dog"
        -- </misc3-5.2>
    })



-- Ticket #626:  make sure EXPLAIN prevents BEGIN and COMMIT from working.
--
test:do_test(
    "misc3-6.1",
    function()
        test:execsql("EXPLAIN BEGIN")
        return test:catchsql("BEGIN")
    end, {
        -- <misc3-6.1>
        0
        -- </misc3-6.1>
    })

test:do_test(
    "misc3-6.2",
    function()
        test:execsql("EXPLAIN COMMIT")
        return test:catchsql("COMMIT")
    end, {
        -- <misc3-6.2>
        0
        -- </misc3-6.2>
    })

test:do_test(
    "misc3-6.3",
    function()
        test:execsql("BEGIN; EXPLAIN ROLLBACK")
        return test:catchsql("ROLLBACK")
    end, {
        -- <misc3-6.3>
        0
        -- </misc3-6.3>
    })

-- Do some additional EXPLAIN operations to exercise the displayP4 logic.
-- This part of test is disabled in scope of #2174
-- test:do_test(
--    "misc3-6.10",
--    function()
--        local x = test:execsql([[
--            CREATE TABLE ex1(
--              id PRIMARY KEY,
--              a INTEGER DEFAULT 54321,
--              b TEXT DEFAULT "hello",
--              c REAL DEFAULT 3.1415926
--            );
--            CREATE UNIQUE INDEX ex1i1 ON ex1(a);
--            EXPLAIN REINDEX;
--        ]])
--        x = json.encode(x)
--        return string.find(x, "\"SorterCompare\",%d+,%d+,%d+") > 0
--    end, true)
--
-- test:do_test(
--     "misc3-6.11-utf8",
--     function()
--         local x = test:execsql([[
--             EXPLAIN SELECT a+123456789012, b*4.5678, c FROM ex1 ORDER BY +a, b DESC
--         ]])
--         x = json.encode(x)
--         local y = {}
--         table.insert(y, string.find(x, "123456789012")>0)
--         table.insert(y, string.find(x, "4.5678")>0)
--         table.insert(y, string.find(x, "hello")>0)
--         table.insert(y, string.find(x, "-B")>0)
--         return y
--     end, {
--         -- <misc3-6.11-utf8>
--         1, 1, 1, 1
--         -- </misc3-6.11-utf8>
--     })



-- MUST_WORK_TEST autoincrement for pk
if (0 > 0) then
    -- Ticket #640:  vdbe stack overflow with a LIMIT clause on a SELECT inside
    -- of a trigger.
    --
    test:do_execsql_test(
        "misc3-7.1",
        [[
            CREATE TABLE y1(a primary key);
            CREATE TABLE y2(b primary key);
            CREATE TABLE y3(c primary key);
            BEGIN;
            CREATE TRIGGER r1 AFTER DELETE ON y1 FOR EACH ROW BEGIN
              INSERT INTO y3(c) SELECT b FROM y2 ORDER BY b LIMIT 1;
            END;
            INSERT INTO y1 VALUES(1);
            INSERT INTO y1 VALUES(2);
            INSERT INTO y1 SELECT a+2 FROM y1;
            INSERT INTO y1 SELECT a+4 FROM y1;
            INSERT INTO y1 SELECT a+8 FROM y1;
            INSERT INTO y1 SELECT a+16 FROM y1;
            INSERT INTO y2 SELECT a FROM y1;
            COMMIT;
            SELECT count(*) FROM y1;
        ]], {
            -- <misc3-7.1>
            32
            -- </misc3-7.1>
        })

    test:do_execsql_test(
        "misc3-7.2",
        [[
            DELETE FROM y1;
            SELECT count(*) FROM y1;
        ]], {
            -- <misc3-7.2>
            0
            -- </misc3-7.2>
        })

    test:do_execsql_test(
        "misc3-7.3",
        [[
            SELECT count(*) FROM y3;
        ]], {
            -- <misc3-7.3>
            32
            -- </misc3-7.3>
        })
    -- endif trigger
    -- Ticket #668: VDBE stack overflow occurs when the left-hand side
    -- of an IN expression is NULL and the result is used as an integer, not
    -- as a jump.
end
test:do_execsql_test(
    "misc-8.1",
    [[
        SELECT count(CASE WHEN b IN ('abc','xyz') THEN 'x' END) FROM t3
    ]], {
        -- <misc-8.1>
        2
        -- </misc-8.1>
    })

test:do_execsql_test(
    "misc-8.2",
    [[
        SELECT count(*) FROM t3 WHERE 1+(b IN ('abc','xyz'))==2
    ]], {
        -- <misc-8.2>
        2
        -- </misc-8.2>
    })

test:finish_test()
