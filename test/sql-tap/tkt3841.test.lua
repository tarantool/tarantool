#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(1)

--!./tcltestrunner.lua
-- 2009 May 7
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
-- Ticket #3841
--
-- The sql_aggregate_count() is not being reset when an aggregate
-- functio is used in a correlated subquery.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


test:do_execsql_test(
    "tkt3841.1",
    [[
        CREATE TABLE table2 (key TEXT primary key, x TEXT);
        CREATE TABLE list (key TEXT, value TEXT primary key);

        INSERT INTO table2 VALUES ('a', 'alist');
        INSERT INTO table2 VALUES ('b', 'blist');
        INSERT INTO list VALUES ('a', '1');
        INSERT INTO list VALUES ('a', '2');
        INSERT INTO list VALUES ('a', '3');
        INSERT INTO list VALUES ('b', '4');
        INSERT INTO list VALUES ('b', '5');
        INSERT INTO list VALUES ('b', '6');

        SELECT
          table2.x,
          (SELECT group_concat(list.value)
            FROM list
            WHERE list.key = table2.key)
        FROM table2;
    ]], {
        -- <tkt3841.1>
        "alist", "1,2,3", "blist", "4,5,6"
        -- </tkt3841.1>
    })

test:finish_test()

