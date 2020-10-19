#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(8)

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
-- This file implements regression tests for sql library.  The
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
        CREATE TABLE "abc5_" (id INT PRIMARY KEY, "#xyz" INT UNIQUE, "!pqr" TEXT );
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
        INSERT INTO "abc5_" VALUES(1, 5,'hello')
    ]], {
        -- <quote-1.1>
        0
        -- </quote-1.1>
    })

test:do_catchsql_test(
    "quote-1.2.1",
    [[
        SELECT * FROM "abc5_"
    ]], {
        -- <quote-1.2.1>
        0, {1, 5, "hello"}
        -- </quote-1.2.1>
    })

test:do_catchsql_test(
    "quote-1.3",
    [[
        SELECT "abc5_"."!pqr", "abc5_"."#xyz"+5 FROM "abc5_"
    ]], {
        -- <quote-1.3>
        0, {"hello", 10}
        -- </quote-1.3>
    })

test:do_catchsql_test(
    "quote-1.3.1",
    [[
        SELECT "!pqr", "#xyz"+5 FROM "abc5_"
    ]], {
        -- <quote-1.3.2>
        0, {"hello", 10}
        -- </quote-1.3.2>
    })

test:do_catchsql_test(
    "quote-1.4",
    [[
        UPDATE "abc5_" SET "#xyz"=11
    ]], {
        -- <quote-1.4>
        0
        -- </quote-1.4>
    })

test:do_catchsql_test(
    "quote-1.5",
    [[
        SELECT "abc5_"."!pqr", "abc5_"."#xyz"+5 FROM "abc5_"
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
        DROP TABLE "abc5_"
    ]], {
        -- <quote-1.6>
        0
        -- </quote-1.6>
    })



test:finish_test()
