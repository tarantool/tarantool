#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(16)

--!./tcltestrunner.lua
-- 2008 July 7
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library.  The
-- focus of this script testing the tokenizer
--
-- $Id: tokenize.test,v 1.1 2008/07/08 00:06:51 drh Exp $
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_catchsql_test(
    "tokenize-1.1",
    [[
        SELECT 1.0e+
    ]], {
        -- <tokenize-1.1>
        1, [[At line 1 at or near position 16: unrecognized token '1.0e']]
        -- </tokenize-1.1>
    })

test:do_catchsql_test(
    "tokenize-1.2",
    [[
        SELECT 1.0E+
    ]], {
        -- <tokenize-1.2>
        1, [[At line 1 at or near position 16: unrecognized token '1.0E']]
        -- </tokenize-1.2>
    })

test:do_catchsql_test(
    "tokenize-1.3",
    [[
        SELECT 1.0e-
    ]], {
        -- <tokenize-1.3>
        1, [[At line 1 at or near position 16: unrecognized token '1.0e']]
        -- </tokenize-1.3>
    })

test:do_catchsql_test(
    "tokenize-1.4",
    [[
        SELECT 1.0E-
    ]], {
        -- <tokenize-1.4>
        1, [[At line 1 at or near position 16: unrecognized token '1.0E']]
        -- </tokenize-1.4>
    })

test:do_catchsql_test(
    "tokenize-1.5",
    [[
        SELECT 1.0e+/
    ]], {
        -- <tokenize-1.5>
        1, [[At line 1 at or near position 16: unrecognized token '1.0e']]
        -- </tokenize-1.5>
    })

test:do_catchsql_test(
    "tokenize-1.6",
    [[
        SELECT 1.0E+:
    ]], {
        -- <tokenize-1.6>
        1, [[At line 1 at or near position 16: unrecognized token '1.0E']]
        -- </tokenize-1.6>
    })

test:do_catchsql_test(
    "tokenize-1.7",
    [[
        SELECT 1.0e-:
    ]], {
        -- <tokenize-1.7>
        1, [[At line 1 at or near position 16: unrecognized token '1.0e']]
        -- </tokenize-1.7>
    })

test:do_catchsql_test(
    "tokenize-1.8",
    [[
        SELECT 1.0E-/
    ]], {
        -- <tokenize-1.8>
        1, [[At line 1 at or near position 16: unrecognized token '1.0E']]
        -- </tokenize-1.8>
    })

test:do_catchsql_test(
    "tokenize-1.9",
    [[
        SELECT 1.0F+5
    ]], {
        -- <tokenize-1.9>
        1, [[At line 1 at or near position 16: unrecognized token '1.0F']]
        -- </tokenize-1.9>
    })

test:do_catchsql_test(
    "tokenize-1.10",
    [[
        SELECT 1.0d-10
    ]], {
        -- <tokenize-1.10>
        1, [[At line 1 at or near position 16: unrecognized token '1.0d']]
        -- </tokenize-1.10>
    })

test:do_catchsql_test(
    "tokenize-1.11",
    [[
        SELECT 1.0e,5
    ]], {
        -- <tokenize-1.11>
        1, [[At line 1 at or near position 16: unrecognized token '1.0e']]
        -- </tokenize-1.11>
    })

test:do_catchsql_test(
    "tokenize-1.12",
    [[
        SELECT 1.0E.10
    ]], {
        -- <tokenize-1.12>
        1, [[At line 1 at or near position 16: unrecognized token '1.0E']]
        -- </tokenize-1.12>
    })

test:do_catchsql_test(
    "tokenize-2.1",
    [[
        SELECT 1, 2 /*]], {
        -- <tokenize-2.1>
        1, [[Syntax error at line 1 near '*']]
        -- </tokenize-2.1>
    })

test:do_catchsql_test(
    "tokenize-2.2",
    [[
        SELECT 1, 2 /*
    ]], {
        -- <tokenize-2.2>
        0, {1, 2}
        -- </tokenize-2.2>
    })

--
--gh-2611 Check the correct parsing of single-line comments.
--
test:do_execsql_test(
    "tokenize-3.1",
    [[
        SELECT 1 + -- 1 + 1.
        1
    ]], {
        -- <tokenize-2.2>
        2
        -- </tokenize-2.2>
    })

test:do_catchsql_test(
    "tokenize-3.2",
    [[
        SELECT 1 + -- Syntax error.
        *
    ]], {
        -- <tokenize-2.2>
        1,"Syntax error at line 2 near '*'"
        -- </tokenize-2.2>
    })

test:finish_test()
