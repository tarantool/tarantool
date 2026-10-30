#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(8)

--!./tcltestrunner.lua
-- 2002 May 24
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
-- This file implements tests for left outer joins containing WHERE
-- clauses that restrict the scope of the left term of the join.
--
-- $Id: join4.test,v 1.4 2005/03/29 03:11:00 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_test(
    "join4-1.1",
    function()
        test:execsql [[
            create table t1(a integer primary key, b varchar(10));
            insert into t1 values(1,'one');
            insert into t1 values(2,'two');
            insert into t1 values(3,'three');
            insert into t1 values(4,'four');

            create table t2(x integer primary key, y varchar(10), z varchar(10));
            insert into t2 values(2,'niban','ok');
            insert into t2 values(4,'yonban','err');
        ]]
        return test:execsql [[
            select * from t1 left outer join t2 on t1.a=t2.x where t2.z='ok'
        ]]
    end, {
        -- <join4-1.1>
        2, "two", 2, "niban", "ok"
        -- </join4-1.1>
    })



test:do_execsql_test(
    "join4-1.2",
    [[
        select * from t1 left outer join t2 on t1.a=t2.x and t2.z='ok'
    ]], {
        -- <join4-1.2>
        1, "one", "", "", "", 2, "two", 2, "niban", "ok", 3, "three", "", "", "", 4, "four", "", "", ""
        -- </join4-1.2>
    })

test:do_test(
    "join4-1.3",
    function()
        test:execsql [[
            create index i2 on t2(z);
        ]]
        return test:execsql [[
            select * from t1 left outer join t2 on t1.a=t2.x where t2.z='ok'
        ]]
    end, {
        -- <join4-1.3>
        2, "two", 2, "niban", "ok"
        -- </join4-1.3>
    })

test:do_execsql_test(
    "join4-1.4",
    [[
        select * from t1 left outer join t2 on t1.a=t2.x and t2.z='ok'
    ]], {
        -- <join4-1.4>
        1, "one", "", "", "", 2, "two", 2, "niban", "ok", 3, "three", "", "", "", 4, "four", "", "", ""
        -- </join4-1.4>
    })

test:do_execsql_test(
    "join4-1.5",
    [[
        select * from t1 left outer join t2 on t1.a=t2.x where t2.z>='ok'
    ]], {
        -- <join4-1.5>
        2, "two", 2, "niban", "ok"
        -- </join4-1.5>
    })

test:do_execsql_test(
    "join4-1.4",
    [[
        select * from t1 left outer join t2 on t1.a=t2.x and t2.z>='ok'
    ]], {
        -- <join4-1.4>
        1, "one", "", "", "", 2, "two", 2, "niban", "ok", 3, "three", "", "", "", 4, "four", "", "", ""
        -- </join4-1.4>
    })

test:do_execsql_test(
    "join4-1.6",
    [[
        select * from t1 left outer join t2 on t1.a=t2.x where t2.z IN ('ok')
    ]], {
        -- <join4-1.6>
        2, "two", 2, "niban", "ok"
        -- </join4-1.6>
    })

test:do_execsql_test(
    "join4-1.7",
    [[
        select * from t1 left outer join t2 on t1.a=t2.x and t2.z IN ('ok')
    ]], {
        -- <join4-1.7>
        1, "one", "", "", "", 2, "two", 2, "niban", "ok", 3, "three", "", "", "", 4, "four", "", "", ""
        -- </join4-1.7>
    })





test:finish_test()
