#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(50)

--!./tcltestrunner.lua
-- 2007 June 8
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
-- focus of this file is testing NULL comparisons in the WHERE clause.
-- See ticket #2404.
--
-- $Id: where5.test,v 1.2 2007/06/08 08:43:10 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Build some test data
--
test:do_test("where5-1.0", function()
    test:execsql [[
        CREATE TABLE t1(x TEXT primary key);
        CREATE TABLE t2(x integer primary key);
        CREATE TABLE t3(x integer PRIMARY KEY);
        INSERT INTO t1 VALUES(-1);
        INSERT INTO t1 VALUES(0);
        INSERT INTO t1 VALUES(1);
        INSERT INTO t2 SELECT * FROM t1;
        INSERT INTO t3 SELECT * FROM t1;
    ]]
    return test:execsql [[
        SELECT * FROM t1 WHERE x<0
    ]]
end, {
    -- <where5-1.0>
    '-1'
    -- </where5-1.0>
})

test:do_execsql_test("where5-1.1", [[
    SELECT * FROM t1 WHERE x<=0
]], {
    -- <where5-1.1>
    '-1', '0'
    -- </where5-1.1>
})

test:do_execsql_test("where5-1.2", [[
    SELECT * FROM t1 WHERE x=0
]], {
    -- <where5-1.2>
    '0'
    -- </where5-1.2>
})

test:do_execsql_test("where5-1.3", [[
    SELECT * FROM t1 WHERE x>=0
]], {
    -- <where5-1.3>
    '0', '1'
    -- </where5-1.3>
})

test:do_execsql_test("where5-1.4", [[
    SELECT * FROM t1 WHERE x>0
]], {
    -- <where5-1.4>
    '1'
    -- </where5-1.4>
})

test:do_execsql_test("where5-1.5", [[
    SELECT * FROM t1 WHERE x<>0
]], {
    -- <where5-1.5>
    '-1', '1'
    -- </where5-1.5>
})

test:do_execsql_test("where5-1.6", [[
    SELECT * FROM t1 WHERE x<NULL
]], {
    -- <where5-1.6>
    
    -- </where5-1.6>
})

test:do_execsql_test("where5-1.7", [[
    SELECT * FROM t1 WHERE x<=NULL
]], {
    -- <where5-1.7>
    
    -- </where5-1.7>
})

test:do_execsql_test("where5-1.8", [[
    SELECT * FROM t1 WHERE x=NULL
]], {
    -- <where5-1.8>
    
    -- </where5-1.8>
})

test:do_execsql_test("where5-1.9", [[
    SELECT * FROM t1 WHERE x>=NULL
]], {
    -- <where5-1.9>
    
    -- </where5-1.9>
})

test:do_execsql_test("where5-1.10", [[
    SELECT * FROM t1 WHERE x>NULL
]], {
    -- <where5-1.10>
    
    -- </where5-1.10>
})

test:do_execsql_test("where5-1.11", [[
    SELECT * FROM t1 WHERE x!=NULL
]], {
    -- <where5-1.11>
    
    -- </where5-1.11>
})

test:do_execsql_test("where5-1.12", [[
    SELECT * FROM t1 WHERE x IS NULL
]], {
    -- <where5-1.12>
    
    -- </where5-1.12>
})

test:do_execsql_test("where5-1.13", [[
    SELECT * FROM t1 WHERE x IS NOT NULL
]], {
    -- <where5-1.13>
    '-1', '0', '1'
    -- </where5-1.13>
})

test:do_execsql_test("where5-2.0", [[
    SELECT * FROM t2 WHERE x<0
]], {
    -- <where5-2.0>
    -1
    -- </where5-2.0>
})

test:do_execsql_test("where5-2.1", [[
    SELECT * FROM t2 WHERE x<=0
]], {
    -- <where5-2.1>
    -1, 0
    -- </where5-2.1>
})

test:do_execsql_test("where5-2.2", [[
    SELECT * FROM t2 WHERE x=0
]], {
    -- <where5-2.2>
    0
    -- </where5-2.2>
})

test:do_execsql_test("where5-2.3", [[
    SELECT * FROM t2 WHERE x>=0
]], {
    -- <where5-2.3>
    0, 1
    -- </where5-2.3>
})

test:do_execsql_test("where5-2.4", [[
    SELECT * FROM t2 WHERE x>0
]], {
    -- <where5-2.4>
    1
    -- </where5-2.4>
})

test:do_execsql_test("where5-2.5", [[
    SELECT * FROM t2 WHERE x<>0
]], {
    -- <where5-2.5>
    -1, 1
    -- </where5-2.5>
})

test:do_execsql_test("where5-2.6", [[
    SELECT * FROM t2 WHERE x<NULL
]], {
    -- <where5-2.6>
    
    -- </where5-2.6>
})

test:do_execsql_test("where5-2.7", [[
    SELECT * FROM t2 WHERE x<=NULL
]], {
    -- <where5-2.7>
    
    -- </where5-2.7>
})

test:do_execsql_test("where5-2.8", [[
    SELECT * FROM t2 WHERE x=NULL
]], {
    -- <where5-2.8>
    
    -- </where5-2.8>
})

test:do_execsql_test("where5-2.9", [[
    SELECT * FROM t2 WHERE x>=NULL
]], {
    -- <where5-2.9>
    
    -- </where5-2.9>
})

test:do_execsql_test("where5-2.10", [[
    SELECT * FROM t2 WHERE x>NULL
]], {
    -- <where5-2.10>
    
    -- </where5-2.10>
})

test:do_execsql_test("where5-2.11", [[
    SELECT * FROM t2 WHERE x!=NULL
]], {
    -- <where5-2.11>
    
    -- </where5-2.11>
})

test:do_execsql_test("where5-2.12", [[
    SELECT * FROM t2 WHERE x IS NULL
]], {
    -- <where5-2.12>
    
    -- </where5-2.12>
})

test:do_execsql_test("where5-2.13", [[
    SELECT * FROM t2 WHERE x IS NOT NULL
]], {
    -- <where5-2.13>
    -1, 0, 1
    -- </where5-2.13>
})

test:do_execsql_test("where5-3.0", [[
    SELECT * FROM t3 WHERE x<0
]], {
    -- <where5-3.0>
    -1
    -- </where5-3.0>
})

test:do_execsql_test("where5-3.1", [[
    SELECT * FROM t3 WHERE x<=0
]], {
    -- <where5-3.1>
    -1, 0
    -- </where5-3.1>
})

test:do_execsql_test("where5-3.2", [[
    SELECT * FROM t3 WHERE x=0
]], {
    -- <where5-3.2>
    0
    -- </where5-3.2>
})

test:do_execsql_test("where5-3.3", [[
    SELECT * FROM t3 WHERE x>=0
]], {
    -- <where5-3.3>
    0, 1
    -- </where5-3.3>
})

test:do_execsql_test("where5-3.4", [[
    SELECT * FROM t3 WHERE x>0
]], {
    -- <where5-3.4>
    1
    -- </where5-3.4>
})

test:do_execsql_test("where5-3.5", [[
    SELECT * FROM t3 WHERE x<>0
]], {
    -- <where5-3.5>
    -1, 1
    -- </where5-3.5>
})

test:do_execsql_test("where5-3.6", [[
    SELECT * FROM t3 WHERE x<NULL
]], {
    -- <where5-3.6>
    
    -- </where5-3.6>
})

test:do_execsql_test("where5-3.7", [[
    SELECT * FROM t3 WHERE x<=NULL
]], {
    -- <where5-3.7>
    
    -- </where5-3.7>
})

test:do_execsql_test("where5-3.8", [[
    SELECT * FROM t3 WHERE x=NULL
]], {
    -- <where5-3.8>
    
    -- </where5-3.8>
})

test:do_execsql_test("where5-3.9", [[
    SELECT * FROM t3 WHERE x>=NULL
]], {
    -- <where5-3.9>
    
    -- </where5-3.9>
})

test:do_execsql_test("where5-3.10", [[
    SELECT * FROM t3 WHERE x>NULL
]], {
    -- <where5-3.10>
    
    -- </where5-3.10>
})

test:do_execsql_test("where5-3.11", [[
    SELECT * FROM t3 WHERE x!=NULL
]], {
    -- <where5-3.11>
    
    -- </where5-3.11>
})

test:do_execsql_test("where5-3.12", [[
    SELECT * FROM t3 WHERE x IS NULL
]], {
    -- <where5-3.12>
    
    -- </where5-3.12>
})

test:do_execsql_test("where5-3.13", [[
    SELECT * FROM t3 WHERE x IS NOT NULL
]], {
    -- <where5-3.13>
    -1, 0, 1
    -- </where5-3.13>
})

test:do_execsql_test("where5-4.0", [[
    SELECT x<NULL FROM t3
]], {
    -- <where5-4.0>
    "", "", ""
    -- </where5-4.0>
})

test:do_execsql_test("where5-4.1", [[
    SELECT x<=NULL FROM t3
]], {
    -- <where5-4.1>
    "", "", ""
    -- </where5-4.1>
})

test:do_execsql_test("where5-4.2", [[
    SELECT x==NULL FROM t3
]], {
    -- <where5-4.2>
    "", "", ""
    -- </where5-4.2>
})

test:do_execsql_test("where5-4.3", [[
    SELECT x>NULL FROM t3
]], {
    -- <where5-4.3>
    "", "", ""
    -- </where5-4.3>
})

test:do_execsql_test("where5-4.4", [[
    SELECT x>=NULL FROM t3
]], {
    -- <where5-4.4>
    "", "", ""
    -- </where5-4.4>
})

test:do_execsql_test("where5-4.5", [[
    SELECT x!=NULL FROM t3
]], {
    -- <where5-4.5>
    "", "", ""
    -- </where5-4.5>
})

test:do_execsql_test("where5-4.6", [[
    SELECT x IS NULL FROM t3
]], {
    -- <where5-4.6>
    0, 0, 0
    -- </where5-4.6>
})

test:do_execsql_test("where5-4.7", [[
    SELECT x IS NOT NULL FROM t3
]], {
    -- <where5-4.7>
    1, 1, 1
    -- </where5-4.7>
})

test:finish_test()

