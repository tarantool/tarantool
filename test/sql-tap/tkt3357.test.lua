#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(4)

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
-- This file implements regression tests for SQLite library.  The
-- focus of this file is testing the fix for ticket #3357.
--
-- $Id: tkt3357.test,v 1.2 2009/06/05 17:09:12 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "tkt3357-1.1",
    [[
        create table a(id integer primary key, b_id integer, myvalue text);
        create table b(id integer primary key, bvalue text);
        insert into a values(1, 1,'Test');
        insert into a values(2, 1,'Test2');
        insert into a values(3, 1,'Test3');
        insert into b values(1, 'btest');
    ]], {
        -- <tkt3357-1.1>
        
        -- </tkt3357-1.1>
    })

test:do_execsql_test(
    "tkt3357-1.2",
    [[
        SELECT cc.id, cc.b_id, cc.myvalue, dd.bvalue 
        FROM (
          SELECT DISTINCT a.id, a.b_id, a.myvalue FROM a
          INNER JOIN b ON a.b_id = b.id WHERE b.bvalue = 'btest'
        ) cc
        LEFT OUTER JOIN b dd ON cc.b_id = dd.id
    ]], {
        -- <tkt3357-1.2>
        1, 1, "Test", "btest", 2, 1, "Test2", "btest", 3, 1, "Test3", "btest"
        -- </tkt3357-1.2>
    })

test:do_execsql_test(
    "tkt3357-1.3",
    [[
        SELECT cc.id, cc.b_id, cc.myvalue
        FROM (
          SELECT a.id, a.b_id, a.myvalue 
          FROM a, b WHERE a.b_id = b.id
        ) cc
        LEFT OUTER JOIN b dd ON cc.b_id = dd.id
    ]], {
        -- <tkt3357-1.3>
        1, 1, "Test", 2, 1, "Test2", 3, 1, "Test3"
        -- </tkt3357-1.3>
    })

test:do_execsql_test(
    "tkt3357-1.4",
    [[
        SELECT cc.id, cc.b_id, cc.myvalue
        FROM (
          SELECT DISTINCT a.id, a.b_id, a.myvalue 
          FROM a, b WHERE a.b_id = b.id
        ) cc
        LEFT OUTER JOIN b dd ON cc.b_id = dd.id
    ]], {
        -- <tkt3357-1.4>
        1, 1, "Test", 2, 1, "Test2", 3, 1, "Test3"
        -- </tkt3357-1.4>
    })

test:finish_test()

