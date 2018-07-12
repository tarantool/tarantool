#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(21)

--!./tcltestrunner.lua
-- 2010 June 09
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
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "tkt-f973c7ac3-1.0",
    [[
        CREATE TABLE t(id INT primary key, c1 INTEGER, c2 INTEGER);
        INSERT INTO t VALUES(1, 5, 5);
        INSERT INTO t VALUES(2, 5, 4);
    ]], {
        -- <tkt-f973c7ac3-1.0>
        
        -- </tkt-f973c7ac3-1.0>
    })

local sqls = {
    "",
    "CREATE INDEX i1 ON t(c1, c2)",
}

for tn, sql in ipairs(sqls) do
    test:execsql(sql)
    test:do_execsql_test(
        "tkt-f973c7ac3-1."..tn..".1",
        [[
            SELECT c1,c2 FROM t WHERE c1 = 5 AND c2>0 AND c2<='2' ORDER BY c2 DESC 
        ]], {
            
        })

    test:do_execsql_test(
        "tkt-f973c7ac3-1."..tn..".2",
        [[
            SELECT c1,c2 FROM t WHERE c1 = 5 AND c2>0 AND c2<=5 ORDER BY c2 DESC 
        ]], {
            5, 5, 5, 4
        })

    test:do_execsql_test(
        "tkt-f973c7ac3-1."..tn..".3",
        [[
            SELECT c1,c2 FROM t WHERE c1 = 5 AND c2>0 AND c2<='5' ORDER BY c2 DESC 
        ]], {
            5, 5, 5, 4
        })

    test:do_execsql_test(
        "tkt-f973c7ac3-1."..tn..".4",
        [[
            SELECT c1,c2 FROM t WHERE c1 = 5 AND c2>'0' AND c2<=5 ORDER BY c2 DESC 
        ]], {
            5, 5, 5, 4
        })

    test:do_execsql_test(
        "tkt-f973c7ac3-1."..tn..".5",
        [[
            SELECT c1,c2 FROM t WHERE c1 = 5 AND c2>'0' AND c2<='5' ORDER BY c2 DESC 
        ]], {
            5, 5, 5, 4
        })

    test:do_execsql_test(
        "tkt-f973c7ac3-1."..tn..".6",
        [[
            SELECT c1,c2 FROM t WHERE c1 = 5 AND c2>0 AND c2<='2' ORDER BY c2 ASC 
        ]], {
            
        })

    test:do_execsql_test(
        "tkt-f973c7ac3-1."..tn..".7",
        [[
            SELECT c1,c2 FROM t WHERE c1 = 5 AND c2>0 AND c2<=5 ORDER BY c2 ASC 
        ]], {
            5, 4, 5, 5
        })

    test:do_execsql_test(
        "tkt-f973c7ac3-1."..tn..".8",
        [[
            SELECT c1,c2 FROM t WHERE c1 = 5 AND c2>0 AND c2<='5' ORDER BY c2 ASC 
        ]], {
            5, 4, 5, 5
        })

    test:do_execsql_test(
        "tkt-f973c7ac3-1."..tn..".9",
        [[
            SELECT c1,c2 FROM t WHERE c1 = 5 AND c2>'0' AND c2<=5 ORDER BY c2 ASC 
        ]], {
            5, 4, 5, 5
        })

    test:do_execsql_test(
        "tkt-f973c7ac3-1."..tn..".10",
        [[
            SELECT c1,c2 FROM t WHERE c1 = 5 AND c2>'0' AND c2<='5' ORDER BY c2 ASC 
        ]], {
            5, 4, 5, 5
        })

end
test:finish_test()

