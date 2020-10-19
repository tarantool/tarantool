#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(7)

--!./tcltestrunner.lua
-- 2011 June 23
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- This file contains tests for sql. Specifically, it tests that sql
-- does not crash and an error is returned if localhost() fails. This 
-- is the problem reported by ticket 91e2e8ba6f.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
local testprefix = "tkt-91e2e8ba6f"
test:do_execsql_test(
    1.1,
    [[
        CREATE TABLE t1(x INTEGER PRIMARY KEY, y NUMBER);
        INSERT INTO t1 VALUES(11, 11);
    ]], {
        -- <1.1>
        
        -- </1.1>
    })

test:do_execsql_test(
    1.2,
    [[
        SELECT x/10, y/10 FROM t1;
    ]], {
        -- <1.2>
        1, 1
        -- </1.2>
    })

test:do_execsql_test(
    1.3,
    [[
        SELECT x/10, y/10 FROM (SELECT * FROM t1);
    ]], {
        -- <1.3>
        1, 1
        -- </1.3>
    })

test:do_execsql_test(
    1.4,
    [[
        SELECT x/10, y/10 FROM (SELECT * FROM t1 LIMIT 5 OFFSET 0);
    ]], {
        -- <1.4>
        1, 1
        -- </1.4>
    })

test:do_execsql_test(
    1.5,
    [[
        SELECT x/10, y/10 FROM (SELECT * FROM t1 LIMIT 5 OFFSET 0) LIMIT 5 OFFSET 0;
    ]], {
        -- <1.5>
        1, 1
        -- </1.5>
    })

test:do_execsql_test(
    1.6,
    [[
        SELECT a.x/10, a.y/10 FROM 
          (SELECT * FROM t1 LIMIT 5 OFFSET 0) AS a, t1 AS b WHERE a.x = b.x
        LIMIT 5 OFFSET 0;
    ]], {
        -- <1.6>
        1, 1
        -- </1.6>
    })

-- MUST_WORK_TEST
if (0 > 0)
 then
end
test:do_execsql_test(
    1.7,
    [[
        CREATE VIEW v1 AS SELECT * FROM t1 LIMIT 5;
        SELECT a.x/10, a.y/10 FROM v1 AS a, t1 AS b WHERE a.x = b.x LIMIT 5 OFFSET 0;
    ]], {
        -- <1.7>
        1, 1
        -- </1.7>
    })

test:finish_test()

