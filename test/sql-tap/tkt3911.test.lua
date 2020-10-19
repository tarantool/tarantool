#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(5)

--!./tcltestrunner.lua
-- 2009 June 11
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
-- Tests to verify ticket #3911 is fixed.
--
-- $Id: tkt3911.test,v 1.1 2009/06/12 03:27:28 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "tkt3911.1",
    [[
        CREATE TABLE t1(a INT primary key,b INT);
        INSERT INTO t1 VALUES(1,2);
        INSERT INTO t1 VALUES(11,12);

        CREATE TABLE t2(b INT primary key,c INT);
        INSERT INTO t2 VALUES(2,3);
        INSERT INTO t2 VALUES(22,23);

        SELECT * FROM t1 JOIN t2 USING(b);
    ]], {
        -- <tkt3911.1>
        1, 2, 3
        -- </tkt3911.1>
    })

test:do_test(
    "tkt3911.2",
    function()
        return test:execsql [[
            SELECT * FROM t1 JOIN (t2) AS x USING (b);
        ]]
    end, {
        -- <tkt3911.2>
        1, 2, 3
        -- </tkt3911.2>
    })

test:do_test(
    "tkt3911.3",
    function()
        return test:execsql [[
            SELECT * FROM t1 JOIN (SELECT * FROM t2) AS x USING (b);
        ]]
    end, {
        -- <tkt3911.3>
        1, 2, 3
        -- </tkt3911.3>
    })

test:do_test(
    "tkt3911.4",
    function()
        return test:execsql [[
            CREATE TABLE t3(m TEXT,a INT primary key);
            INSERT INTO t3 VALUES('one',1);
            INSERT INTO t3 VALUES('two',2);

            SELECT * FROM t3 JOIN (SELECT * FROM t1 NATURAL JOIN t2) AS x USING(a);
        ]]
    end, {
        -- <tkt3911.4>
        "one", 1, 2, 3
        -- </tkt3911.4>
    })

test:do_test(
    "tkt3911.5",
    function()
        return test:execsql [[
            SELECT * FROM t3 JOIN (SELECT * FROM t1 JOIN t2 USING (b)) AS x USING(a);
        ]]
    end, {
        -- <tkt3911.5>
        "one", 1, 2, 3
        -- </tkt3911.5>
    })

test:finish_test()

