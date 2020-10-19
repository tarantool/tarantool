#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(4)

--!./tcltestrunner.lua
-- 2008 October 20
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library.
--
-- This file implements tests to verify that ticket #3442 has been
-- fixed.  
--
--
-- $Id: tkt3442.test,v 1.2 2009/06/05 17:09:12 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Create a schema with some indexes.
--
test:do_execsql_test(
    "tkt3442-1.1",
    [[
        CREATE TABLE listhash(
          key INTEGER PRIMARY KEY,
          id TEXT,
          node INTEGER
        );
        CREATE UNIQUE INDEX ididx ON listhash(id);
    ]], {
        -- <tkt3442-1.1>
        
        -- </tkt3442-1.1>
    })

-- Explain Query Plan
--
local function EQP(sql)
    return test:execsql("EXPLAIN QUERY PLAN "..sql)
end

-- These tests perform an EXPLAIN QUERY PLAN on both versions of
-- SELECT: with string literal and numeric constant and verify
-- that the query plans are different.
--
test:do_test(
    "tkt3442-1.2",
    function()
        return EQP(" SELECT node FROM listhash WHERE id='5000' LIMIT 1; ")
    end, {
        -- <tkt3442-1.2>
        0, 0, 0, "SEARCH TABLE LISTHASH USING COVERING INDEX IDIDX (ID=?) (~1 row)"
        -- </tkt3442-1.2>
    })

test:do_test(
    "tkt3442-1.3",
    function()
        return EQP([[ SELECT node FROM listhash WHERE id=5000 LIMIT 1; ]])
    end, {
        -- <tkt3442-1.3>
        0, 0, 0, "SCAN TABLE LISTHASH (~262144 rows)"
        -- </tkt3442-1.3>
    })

test:do_catchsql_test(
    "tkt3442-1.4",
    [[
        SELECT node FROM listhash WHERE id="5000" LIMIT 1;
    ]], {
        -- <tkt3442-1.5>
        1, "Can’t resolve field '5000'"
        -- </tkt3442-1.5>
    })

test:finish_test()

