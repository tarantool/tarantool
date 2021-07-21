#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(12)

--!./tcltestrunner.lua
-- 2014-03-24
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
-- Tests to verify that arithmetic operators do not change the type of
-- input operands.  Ticket [a8a0d2996a]
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_catchsql_test(
    1.0,
    [[
        CREATE TABLE t(id INT PRIMARY KEY, x TEXT UNIQUE, y TEXT);
        INSERT INTO t VALUES(1, '1','1');
        SELECT typeof(x), typeof(y) FROM t WHERE 1=x+0 AND y=='1';
    ]], {
        -- <1.0>
        1, "Type mismatch: can not convert string('1') to number"
        -- </1.0>
    })

test:do_catchsql_test(
    1.1,
    [[
        SELECT typeof(x), typeof(y) FROM t WHERE 1=x-0 AND y=='1';
    ]], {
        -- <1.1>
        1, "Type mismatch: can not convert string('1') to number"
        -- </1.1>
    })

test:do_catchsql_test(
    1.2,
    [[
        SELECT typeof(x), typeof(y) FROM t WHERE 1=x*1 AND y=='1';
    ]], {
        -- <1.2>
        1, "Type mismatch: can not convert string('1') to number"
        -- </1.2>
    })

test:do_catchsql_test(
    1.3,
    [[
        SELECT typeof(x), typeof(y) FROM t WHERE 1=x/1 AND y=='1';
    ]], {
        -- <1.3>
        1, "Type mismatch: can not convert string('1') to number"
        -- </1.3>
    })

test:do_catchsql_test(
    1.4,
    [[
        SELECT typeof(x), typeof(y) FROM t WHERE 1=x%4 AND y=='1';
    ]], {
        -- <1.4>
        1, "Type mismatch: can not convert string('1') to integer"
        -- </1.4>
    })

test:do_catchsql_test(
    3.0,
    [[
        UPDATE t SET x='1.0';
        SELECT typeof(x), typeof(y) FROM t WHERE 1=x+0 AND y=='1';
    ]], {
        -- <3.0>
        1, "Type mismatch: can not convert string('1.0') to number"
        -- </3.0>
    })

test:do_catchsql_test(
    3.1,
    [[
        SELECT typeof(x), typeof(y) FROM t WHERE 1=x-0 AND y=='1';
    ]], {
        -- <3.1>
        1, "Type mismatch: can not convert string('1.0') to number"
        -- </3.1>
    })

test:do_catchsql_test(
    3.2,
    [[
        SELECT typeof(x), typeof(y) FROM t WHERE 1=x*1 AND y=='1';
    ]], {
        -- <3.2>
        1, "Type mismatch: can not convert string('1.0') to number"
        -- </3.2>
    })

test:do_catchsql_test(
    3.3,
    [[
        SELECT typeof(x), typeof(y) FROM t WHERE 1=x/1 AND y=='1';
    ]], {
        -- <3.3>
        1, "Type mismatch: can not convert string('1.0') to number"
        -- </3.3>
    })

test:do_catchsql_test(
    3.4,
    [[
        SELECT typeof(x), typeof(y) FROM t WHERE 1=x%4 AND y=='1';
    ]], {
        -- <3.4>
        1, "Type mismatch: can not convert string('1.0') to integer"
        -- </3.4>
    })

test:do_execsql_test(
    4.0,
    [[
        SELECT 1+1.;
    ]], {
        -- <4.0>
        2.0
        -- </4.0>
    })

test:do_catchsql_test(
    4.1,
    [[
        SELECT '1.23e64'/'1.0000e+62';
    ]], {
        -- <4.1>
        1, "Type mismatch: can not convert string('1.0000e+62') to number"
        -- </4.1>
    })

test:finish_test()
