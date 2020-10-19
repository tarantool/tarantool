#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(26)

--!./tcltestrunner.lua
-- 2008 October 13
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library. Specifically,
-- it tests that affinities and collation sequences are correctly applied
-- in aggregate queries.
--
-- $Id: tkt3493.test,v 1.2 2009/06/05 17:09:12 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "tkt3493-1.1",
    [[
        CREATE TABLE A (id INTEGER PRIMARY KEY, val TEXT);
        CREATE TABLE B (id INTEGER PRIMARY KEY, val TEXT);
        CREATE TABLE A_B (B_id INTEGER NOT NULL PRIMARY KEY, A_id INTEGER);
        START TRANSACTION;
        INSERT INTO A VALUES(1,'123');
        INSERT INTO A VALUES(2,'456');
        INSERT INTO B VALUES(1,'1');
        INSERT INTO B VALUES(2,'2');
        INSERT INTO A_B VALUES(1,1);
        INSERT INTO A_B VALUES(2,2);
        COMMIT;
    ]], {
        -- <tkt3493-1.1>
        
        -- </tkt3493-1.1>
    })

test:do_execsql_test(
    "tkt3493-1.2",
    [[
        SELECT 
          CASE 
             WHEN B.val = 1 THEN 'XYZ' 
             ELSE A.val 
          END AS Col1
        FROM B  
        LEFT OUTER JOIN A_B ON B.id = A_B.B_id  
        LEFT OUTER JOIN A ON A.id = A_B.A_id
        ORDER BY Col1 ASC;
    ]], {
        -- <tkt3493-1.2>
        "456", "XYZ"
        -- </tkt3493-1.2>
    })

test:do_execsql_test(
    "tkt3493-1.3",
    [[
        SELECT DISTINCT
          CASE 
             WHEN B.val = 1 THEN 'XYZ' 
             ELSE A.val 
          END AS Col1
        FROM B  
        LEFT OUTER JOIN A_B ON B.id = A_B.B_id  
        LEFT OUTER JOIN A ON A.id = A_B.A_id
        ORDER BY Col1 ASC;
    ]], {
        -- <tkt3493-1.3>
        "456", "XYZ"
        -- </tkt3493-1.3>
    })

test:do_execsql_test(
    "tkt3493-1.4",
    [[
        SELECT b.val, CASE WHEN b.val = 1 THEN 'xyz' ELSE b.val END AS col1 FROM b;
    ]], {
        -- <tkt3493-1.4>
        "1", "xyz", "2", "2"
        -- </tkt3493-1.4>
    })

test:do_execsql_test(
    "tkt3493-1.5",
    [[
        SELECT DISTINCT 
          b.val, 
          CASE WHEN b.val = 1 THEN 'xyz' ELSE b.val END AS col1 
        FROM b;
    ]], {
        -- <tkt3493-1.5>
        "1", "xyz", "2", "2"
        -- </tkt3493-1.5>
    })

test:do_execsql_test(
    "tkt3493-1.6",
    [[
        SELECT DISTINCT 
          b.val, 
          CASE WHEN b.val = '1' THEN 'xyz' ELSE b.val END AS col1 
        FROM b;
    ]], {
        -- <tkt3493-1.6>
        "1", "xyz", "2", "2"
        -- </tkt3493-1.6>
    })

test:do_execsql_test(
    "tkt3493-2.1",
    [[
        CREATE TABLE t1(a TEXT PRIMARY KEY, b INT);
        INSERT INTO t1 VALUES('123', 456);
    ]], {
        -- <tkt3493-2.1>
        
        -- </tkt3493-2.1>
    })

test:do_execsql_test(
    "tkt3493-2.2.1",
    [[
        SELECT a=123 FROM t1 GROUP BY a 
    ]], {
        -- <tkt3493-2.2.1>
        true
        -- </tkt3493-2.2.1>
    })

test:do_execsql_test(
    "tkt3493-2.2.2",
    [[
        SELECT a=123 FROM t1 
    ]], {
        -- <tkt3493-2.2.2>
        true
        -- </tkt3493-2.2.2>
    })

test:do_execsql_test(
    "tkt3493-2.2.3",
    [[
        SELECT a='123' FROM t1 
    ]], {
        -- <tkt3493-2.2.3>
        true
        -- </tkt3493-2.2.3>
    })

test:do_execsql_test(
    "tkt3493-2.2.4",
    [[
        SELECT count(*), a=123 FROM t1 
    ]], {
        -- <tkt3493-2.2.4>
        1, true
        -- </tkt3493-2.2.4>
    })

test:do_execsql_test(
    "tkt3493-2.2.5",
    [[
        SELECT count(*), +a=123 FROM t1 
    ]], {
        -- <tkt3493-2.2.5>
        1, true
        -- </tkt3493-2.2.5>
    })

test:do_execsql_test(
    "tkt3493-2.3.3",
    [[
        SELECT b='456' FROM t1 GROUP BY a 
    ]], {
        -- <tkt3493-2.3.3>
        true
        -- </tkt3493-2.3.3>
    })

test:do_execsql_test(
    "tkt3493-2.3.1",
    [[
        SELECT b='456' FROM t1 GROUP BY b 
    ]], {
        -- <tkt3493-2.3.1>
        true
        -- </tkt3493-2.3.1>
    })

test:do_execsql_test(
    "tkt3493-2.3.2",
    [[
        SELECT b='456' FROM t1 
    ]], {
        -- <tkt3493-2.3.2>
        true
        -- </tkt3493-2.3.2>
    })

test:do_execsql_test(
    "tkt3493-2.4.1",
    [[
        SELECT typeof(a), a FROM t1 GROUP BY a HAVING a=123 
    ]], {
        -- <tkt3493-2.4.1>
        "string", "123"
        -- </tkt3493-2.4.1>
    })

test:do_execsql_test(
    "tkt3493-2.4.2",
    [[
        SELECT typeof(a), a FROM t1 GROUP BY b HAVING a=123 
    ]], {
        -- <tkt3493-2.4.2>
        "string", "123"
        -- </tkt3493-2.4.2>
    })

test:do_execsql_test(
    "tkt3493-2.5.1",
    [[
        SELECT typeof(b), b FROM t1 GROUP BY a HAVING b='456' 
    ]], {
        -- <tkt3493-2.5.1>
        "integer", 456
        -- </tkt3493-2.5.1>
    })

test:do_execsql_test(
    "tkt3493-2.5.2",
    [[
        SELECT typeof(b), b FROM t1 GROUP BY b HAVING b='456' 
    ]], {
        -- <tkt3493-2.5.2>
        "integer", 456
        -- </tkt3493-2.5.2>
    })

test:do_execsql_test(
    "tkt3493-3.1",
    [[
        CREATE TABLE t2(a  TEXT COLLATE "unicode_ci" PRIMARY KEY, b  TEXT);
        INSERT INTO t2 VALUES('aBc', 'DeF');
    ]], {
        -- <tkt3493-3.1>
        
        -- </tkt3493-3.1>
    })

-- MUST_WORK_TEST
if (0 > 0)
 then
end
test:do_execsql_test(
    "tkt3493-3.2.1",
    [[
        SELECT a='abc' FROM t2 GROUP BY a 
    ]], {
        -- <tkt3493-3.2.1>
        true
        -- </tkt3493-3.2.1>
    })

test:do_execsql_test(
    "tkt3493-3.2.2",
    [[
        SELECT a='abc' FROM t2 
    ]], {
        -- <tkt3493-3.2.2>
        true
        -- </tkt3493-3.2.2>
    })

test:do_execsql_test(
    "tkt3493-3.3.1",
    [[
        SELECT a>b FROM t2 GROUP BY a, b
    ]], {
        -- <tkt3493-3.3.1>
        false
        -- </tkt3493-3.3.1>
    })

test:do_execsql_test(
    "tkt3493-3.3.2",
    [[
        SELECT a>b COLLATE "binary" FROM t2 GROUP BY a, b
    ]], {
        -- <tkt3493-3.3.2>
        true
        -- </tkt3493-3.3.2>
    })

test:do_execsql_test(
    "tkt3493-3.3.3",
    [[
        SELECT b>a FROM t2 GROUP BY a, b
    ]], {
        -- <tkt3493-3.3.3>
        true
        -- </tkt3493-3.3.3>
    })

test:do_execsql_test(
    "tkt3493-3.3.4",
    [[
        SELECT b>a COLLATE "unicode_ci" FROM t2 GROUP BY a, b
    ]], {
        -- <tkt3493-3.3.4>
        true
        -- </tkt3493-3.3.4>
    })

test:finish_test()

