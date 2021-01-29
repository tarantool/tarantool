#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(2)

--!./tcltestrunner.lua
-- 2008 December 4
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
-- This file is a verification that the bugs identified in ticket
-- #3522 have been fixed.
--
-- $Id: tkt3522.test,v 1.1 2008/12/05 00:00:07 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_test(
    "tkt3522-1.1",
    function()
        return test:execsql [[
            CREATE TABLE tab4(
               col0 INTEGER primary key,
               col1 INTEGER,
               col2 INTEGER,
               col3 INTEGER,
               col4 INTEGER
            );
            SELECT cor1.*
              FROM tab4 AS cor0
              JOIN tab4 AS cor1 USING ( col4, col3, col2, col1, col0 );
        ]]
    end, {
        -- <tkt3522-1.1>

        -- </tkt3522-1.1>
    })

test:do_test(
    "tkt3522-1.2",
    function()
        return test:execsql [[
            CREATE TABLE tab1(col0 INTEGER primary key);
            CREATE TABLE tab2(col0 INTEGER primary key);
            SELECT cor0.* FROM tab1 NATURAL JOIN tab2 AS cor0;
        ]]
    end, {
        -- <tkt3522-1.2>

        -- </tkt3522-1.2>
    })

test:finish_test()

