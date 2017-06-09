#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(13)

--!./tcltestrunner.lua
-- 2001 September 15
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library.  The
-- focus of this file is the ability to specify table and column names
-- as quoted strings.
--
-- $Id: quote.test,v 1.7 2007/04/25 11:32:30 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- MUST_WORK_TEST
--Create a table with a strange name and with strange column names.

test:do_catchsql_test(
    "quote-1.0",
    [[
        --- CREATE TABLE '@abc' ( '#xyz' int PRIMARY KEY, '!pqr' text );
        CREATE TABLE '_5abc' ( '#xyz' int PRIMARY KEY, '!pqr' text );
    ]], {
        -- <quote-1.0>
        0
        -- </quote-1.0>
    })

-- Insert, update and query the table.
--
test:do_catchsql_test(
    "quote-1.1",
    [[
        INSERT INTO '_5abc' VALUES(5,'hello')
    ]], {
        -- <quote-1.1>
        0
        -- </quote-1.1>
    })

test:do_catchsql_test(
    "quote-1.2.1",
    [[
        SELECT * FROM '_5abc'
    ]], {
        -- <quote-1.2.1>
        0, {5, "hello"}
        -- </quote-1.2.1>
    })

test:do_test(
    "quote-1.2.2",
    function()
        return test:catchsql "SELECT * FROM [_5abc]"
        -- SqlServer compatibility
    end, {
        -- <quote-1.2.2>
        0, {5, "hello"}
        -- </quote-1.2.2>
    })

test:do_test(
    "quote-1.2.3",
    function()
        return test:catchsql "SELECT * FROM `_5abc`"
        -- MySQL compatibility
    end, {
        -- <quote-1.2.3>
        0, {5, "hello"}
        -- </quote-1.2.3>
    })

test:do_catchsql_test(
    "quote-1.3",
    [[
        SELECT '_5abc'.'!pqr', '_5abc'.'#xyz'+5 FROM '_5abc'
    ]], {
        -- <quote-1.3>
        0, {"hello", 10}
        -- </quote-1.3>
    })

test:do_catchsql_test(
    "quote-1.3.1",
    [[
        SELECT '!pqr', '#xyz'+5 FROM '_5abc'
    ]], {
        -- <quote-1.3.1>
        0, {"!pqr", 5}
        -- </quote-1.3.1>
    })

test:do_catchsql_test(
    "quote-1.3.2",
    [[
        SELECT "!pqr", "#xyz"+5 FROM '_5abc'
    ]], {
        -- <quote-1.3.2>
        0, {"hello", 10}
        -- </quote-1.3.2>
    })

test:do_catchsql_test(
    "quote-1.3.3",
    [=[
        SELECT [!pqr], `#xyz`+5 FROM '_5abc'
    ]=], {
        -- <quote-1.3.3>
        0, {"hello", 10}
        -- </quote-1.3.3>
    })

test:do_catchsql_test(
    "quote-1.3.4",
    [[
        SELECT '_5abc'.'!pqr', '_5abc'.'#xyz'+5 FROM '_5abc'
    ]], {
        -- <quote-1.3.4>
        0, {"hello", 10}
        -- </quote-1.3.4>
    })

test:do_catchsql_test(
    "quote-1.4",
    [[
        UPDATE '_5abc' SET '#xyz'=11
    ]], {
        -- <quote-1.4>
        0
        -- </quote-1.4>
    })

test:do_catchsql_test(
    "quote-1.5",
    [[
        SELECT '_5abc'.'!pqr', '_5abc'.'#xyz'+5 FROM '_5abc'
    ]], {
        -- <quote-1.5>
        0, {"hello", 16}
        -- </quote-1.5>
    })

-- Drop the table with the strange name.
--
test:do_catchsql_test(
    "quote-1.6",
    [[
        DROP TABLE '_5abc'
    ]], {
        -- <quote-1.6>
        0
        -- </quote-1.6>
    })



test:finish_test()
