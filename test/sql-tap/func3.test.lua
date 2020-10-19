#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(35)

--!./tcltestrunner.lua
-- 2010 August 27
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library. The
-- focus of this file is testing that destructor functions associated
-- with functions created using sql_create_function_v2() is
-- correctly invoked.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- MUST_WORK_TEST built-in functions check desrtoy api of sql_create_function_v2

-- EVIDENCE-OF: R-41921-05214 The likelihood(X,Y) function returns
-- argument X unchanged.
--
test:do_execsql_test(
    "func3-5.1",
    [[
        SELECT likelihood(9223372036854775807, 0.5);
    ]], {
        -- <func3-5.1>
        9223372036854775807LL
        -- </func3-5.1>
    })

test:do_execsql_test(
    "func3-5.2",
    [[
        SELECT likelihood(-9223372036854775808, 0.5);
    ]], {
        -- <func3-5.2>
        -9223372036854775808LL
        -- </func3-5.2>
    })

test:do_execsql_test(
    "func3-5.3",
    [[
        SELECT likelihood(14.125, 0.5);
    ]], {
        -- <func3-5.3>
        14.125
        -- </func3-5.3>
    })

test:do_execsql_test(
    "func3-5.4",
    [[
        SELECT likelihood(NULL, 0.5);
    ]], {
        -- <func3-5.4>
        ""
        -- </func3-5.4>
    })

test:do_execsql_test(
    "func3-5.5",
    [[
        SELECT likelihood('test-string', 0.5);
    ]], {
        -- <func3-5.5>
        "test-string"
        -- </func3-5.5>
    })

test:do_execsql_test(
    "func3-5.6",
    [[
        SELECT quote(likelihood(x'010203000405', 0.5));
    ]], {
        -- <func3-5.6>
        "X'010203000405'"
        -- </func3-5.6>
    })

-- EVIDENCE-OF: R-44133-61651 The value Y in likelihood(X,Y) must be a
-- floating point constant between 0.0 and 1.0, inclusive.
--
test:do_execsql_test(
    "func3-5.7",
    [[
        SELECT likelihood(123, 1.0), likelihood(456, 0.0);
    ]], {
        -- <func3-5.7>
        123, 456
        -- </func3-5.7>
    })

test:do_catchsql_test(
    "func3-5.8",
    [[
        SELECT likelihood(123, 1.000001);
    ]], {
        -- <func3-5.8>
        1, "Illegal parameters, second argument to likelihood() must be a constant between 0.0 and 1.0"
        -- </func3-5.8>
    })

test:do_catchsql_test(
    "func3-5.9",
    [[
        SELECT likelihood(123, -0.000001);
    ]], {
        -- <func3-5.9>
        1, "Illegal parameters, second argument to likelihood() must be a constant between 0.0 and 1.0"
        -- </func3-5.9>
    })

test:do_catchsql_test(
    "func3-5.10",
    [[
        SELECT likelihood(123, 0.5+0.3);
    ]], {
        -- <func3-5.10>
        1, "Illegal parameters, second argument to likelihood() must be a constant between 0.0 and 1.0"
        -- </func3-5.10>
    })

-- EVIDENCE-OF: R-28535-44631 The likelihood(X) function is a no-op that
-- the code generator optimizes away so that it consumes no CPU cycles
-- during run-time (that is, during calls to sql_step()).
--
test:do_test(
    "func3-5.20",
    function()
        return test:execsql "EXPLAIN SELECT likelihood(LEAST(1.0+'2.0',4*11), 0.5)"
    end, test:execsql("EXPLAIN SELECT LEAST(1.0+'2.0',4*11)"))

-- EVIDENCE-OF: R-11152-23456 The unlikely(X) function returns the
-- argument X unchanged.
--
test:do_execsql_test(
    "func3-5.30",
    [[
        SELECT unlikely(9223372036854775807);
    ]], {
        -- <func3-5.30>
        9223372036854775807LL
        -- </func3-5.30>
    })

test:do_execsql_test(
    "func3-5.31",
    [[
        SELECT unlikely(-9223372036854775808);
    ]], {
        -- <func3-5.31>
        -9223372036854775808LL
        -- </func3-5.31>
    })

test:do_execsql_test(
    "func3-5.32",
    [[
        SELECT unlikely(14.125);
    ]], {
        -- <func3-5.32>
        14.125
        -- </func3-5.32>
    })

test:do_execsql_test(
    "func3-5.33",
    [[
        SELECT unlikely(NULL);
    ]], {
        -- <func3-5.33>
        ""
        -- </func3-5.33>
    })

test:do_execsql_test(
    "func3-5.34",
    [[
        SELECT unlikely('test-string');
    ]], {
        -- <func3-5.34>
        "test-string"
        -- </func3-5.34>
    })

test:do_execsql_test(
    "func3-5.35",
    [[
        SELECT quote(unlikely(x'010203000405'));
    ]], {
        -- <func3-5.35>
        "X'010203000405'"
        -- </func3-5.35>
    })

-- EVIDENCE-OF: R-22887-63324 The unlikely(X) function is a no-op that
-- the code generator optimizes away so that it consumes no CPU cycles at
-- run-time (that is, during calls to sql_step()).
--
test:do_test(
    "func3-5.39",
    function()
        return test:execsql "EXPLAIN SELECT unlikely(LEAST(1.0+'2.0',4*11))"
    end, test:execsql "EXPLAIN SELECT LEAST(1.0+'2.0',4*11)")

-- EVIDENCE-OF: R-23735-03107 The likely(X) function returns the argument
-- X unchanged.
--
test:do_execsql_test(
    "func3-5.50",
    [[
        SELECT likely(9223372036854775807);
    ]], {
        -- <func3-5.50>
        9223372036854775807LL
        -- </func3-5.50>
    })

test:do_execsql_test(
    "func3-5.51",
    [[
        SELECT likely(-9223372036854775808);
    ]], {
        -- <func3-5.51>
        -9223372036854775808LL
        -- </func3-5.51>
    })

test:do_execsql_test(
    "func3-5.52",
    [[
        SELECT likely(14.125);
    ]], {
        -- <func3-5.52>
        14.125
        -- </func3-5.52>
    })

test:do_execsql_test(
    "func3-5.53",
    [[
        SELECT likely(NULL);
    ]], {
        -- <func3-5.53>
        ""
        -- </func3-5.53>
    })

test:do_execsql_test(
    "func3-5.54",
    [[
        SELECT likely('test-string');
    ]], {
        -- <func3-5.54>
        "test-string"
        -- </func3-5.54>
    })

test:do_execsql_test(
    "func3-5.55",
    [[
        SELECT quote(likely(x'010203000405'));
    ]], {
        -- <func3-5.55>
        "X'010203000405'"
        -- </func3-5.55>
    })

-- EVIDENCE-OF: R-43464-09689 The likely(X) function is a no-op that the
-- code generator optimizes away so that it consumes no CPU cycles at
-- run-time (that is, during calls to sql_step()).
--
test:do_test(
    "func3-5.59",
    function()
        return test:execsql "EXPLAIN SELECT likely(LEAST(1.0+'2.0',4*11))"
    end, test:execsql "EXPLAIN SELECT LEAST(1.0+'2.0',4*11)")


--
-- gh-3929: sql: ANSI aliases for LENGTH().
--
local suits = {}
suits[1] = {str = '123456789', len = 9}
suits[2] = {str = '\x80', len = 1}
suits[3] = {str = '\x61\x62\x63', len = 3}
suits[4] = {str = '\x7f\x80\x81', len = 3}
suits[5] = {str = '\x61\xc0', len = 2}
suits[6] = {str = '\x61\xc0\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80', len = 12}
suits[7] = {str = '\xc0\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80', len = 11}
suits[8] = {str = '\x80\x80\x80\x80\x80\x80\x80\x80\x80\x80', len = 10}
suits[9] = {str = '\x80\x80\x80\x80\x80\xf0\x80\x80\x80\x80', len = 7}
suits[10] = {str = '\x80\x80\x80\x80\x80\xf0\x80\x80\x80\xff', len = 7}

for k,v in pairs(suits) do
    test:do_execsql_test(
        "func3-6."..k,
        "SELECT CHAR_LENGTH('"..v.str.."'), CHARACTER_LENGTH('"..v.str.."');",
        {v.len, v.len})
end

test:finish_test()
