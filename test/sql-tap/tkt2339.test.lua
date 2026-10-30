#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(9)

--!./tcltestrunner.lua
-- 2007 May 6
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
-- $Id: tkt2339.test,v 1.2 2007/09/12 17:01:45 danielk1977 Exp $
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


test:do_execsql_test(
    "tkt2339.1",
    [[
        create table t1(numb int primary key);
        insert into t1 values (1);
        insert into t1 values (2);
        insert into t1 values (3);
        insert into t1 values (4);

        create table t2(numb int primary key);
        insert into t2 values (11);
        insert into t2 values (12);
        insert into t2 values (13);
        insert into t2 values (14);

        SELECT * FROM (SELECT * FROM t1 ORDER BY numb DESC LIMIT 2)
        UNION
        SELECT * FROM (SELECT * FROM t2 ORDER BY numb DESC LIMIT 2)
    ]], {
        -- <tkt2339.1>
        3, 4, 13, 14
        -- </tkt2339.1>
    })

test:do_execsql_test(
    "tkt2339.2",
    [[
        SELECT * FROM (SELECT * FROM t1 ORDER BY numb DESC LIMIT 2)
        UNION ALL
        SELECT * FROM (SELECT * FROM t2 ORDER BY numb DESC LIMIT 2)
    ]], {
        -- <tkt2339.2>
        4, 3, 14, 13
        -- </tkt2339.2>
    })

test:do_execsql_test(
    "tkt2339.3",
    [[
        SELECT * FROM (SELECT * FROM t1 ORDER BY numb DESC)
        UNION ALL
        SELECT * FROM (SELECT * FROM t2 ORDER BY numb DESC LIMIT 2)
    ]], {
        -- <tkt2339.3>
        4, 3, 2, 1, 14, 13
        -- </tkt2339.3>
    })

test:do_execsql_test(
    "tkt2339.4",
    [[
        SELECT * FROM (SELECT * FROM t1 ORDER BY numb DESC LIMIT 2)
        UNION ALL
        SELECT * FROM (SELECT * FROM t2 ORDER BY numb DESC)
    ]], {
        -- <tkt2339.4>
        4, 3, 14, 13, 12, 11
        -- </tkt2339.4>
    })

test:do_execsql_test(
    "tkt2339.5",
    [[
        SELECT * FROM (SELECT * FROM t1 ORDER BY numb DESC LIMIT 2)
        UNION
        SELECT * FROM (SELECT * FROM t2 ORDER BY numb DESC)
    ]], {
        -- <tkt2339.5>
        3, 4, 11, 12, 13, 14
        -- </tkt2339.5>
    })

test:do_execsql_test(
    "tkt2339.6",
    [[
        SELECT * FROM (SELECT * FROM t1 ORDER BY numb DESC LIMIT 2)
        EXCEPT
        SELECT * FROM (SELECT * FROM t2 ORDER BY numb DESC)
    ]], {
        -- <tkt2339.6>
        3, 4
        -- </tkt2339.6>
    })

test:do_execsql_test(
    "tkt2339.7",
    [[
        SELECT * FROM (SELECT * FROM t1 LIMIT 2)
        UNION
        SELECT * FROM (SELECT * FROM t2 ORDER BY numb DESC LIMIT 2)
    ]], {
        -- <tkt2339.7>
        1, 2, 13, 14
        -- </tkt2339.7>
    })

test:do_execsql_test(
    "tkt2339.8",
    [[
        SELECT * FROM (SELECT * FROM t1 LIMIT 2)
        UNION
        SELECT * FROM (SELECT * FROM t2 LIMIT 2)
    ]], {
        -- <tkt2339.8>
        1, 2, 11, 12
        -- </tkt2339.8>
    })

test:do_execsql_test(
    "tkt2339.9",
    [[
        SELECT * FROM (SELECT * FROM t1 ORDER BY numb DESC LIMIT 2)
        UNION
        SELECT * FROM (SELECT * FROM t2 LIMIT 2)
    ]], {
        -- <tkt2339.9>
        3, 4, 11, 12
        -- </tkt2339.9>
    })

test:finish_test()

