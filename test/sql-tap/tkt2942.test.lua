#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(4)

--!./tcltestrunner.lua
-- 2008 February 15
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
-- Ticket #2942.  
--
-- Queries of the form:
--
--     SELECT group_concat(x) FROM (SELECT * FROM table ORDER BY 1);
--
-- The ORDER BY would be dropped by the query flattener.  This used
-- to not matter because aggregate functions sum(), min(), max(), avg(),
-- and so forth give the same result regardless of the order of inputs.
-- But with the addition of the group_concat() function, suddenly the
-- order does matter.
--
-- $Id: tkt2942.test,v 1.1 2008/02/15 14:33:04 drh Exp $
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


test:do_execsql_test(
    "tkt2942.1",
    [[
        create table t1(id  INT primary key, "num" int);
        insert into t1 values (1, 2);
        insert into t1 values (2, 1);
        insert into t1 values (3, 3);
        insert into t1 values (4, 4);
        SELECT group_concat("num") FROM (SELECT "num" FROM t1 ORDER BY "num" DESC);
    ]], {
        -- <tkt2942.1>
        "4,3,2,1"
        -- </tkt2942.1>
    })

test:do_execsql_test(
    "tkt2942.2",
    [[
        SELECT group_concat("num") FROM (SELECT "num" FROM t1 ORDER BY "num");
    ]], {
        -- <tkt2942.2>
        "1,2,3,4"
        -- </tkt2942.2>
    })

test:do_execsql_test(
    "tkt2942.3",
    [[
        SELECT group_concat("num") FROM (SELECT "num" FROM t1);
    ]], {
        -- <tkt2942.3>
        "2,1,3,4"
        -- </tkt2942.3>
    })

test:do_execsql_test(
    "tkt2942.4",
    [[
        SELECT group_concat("num") FROM (SELECT "num" FROM t1 ORDER BY id DESC);
    ]], {
        -- <tkt2942.4>
        "4,3,1,2"
        -- </tkt2942.4>
    })

test:finish_test()

