#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(5)

--!./tcltestrunner.lua
-- 2005 August 18
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
-- This file implements tests to increase coverage of trigger.c.
--
-- $Id: trigger7.test,v 1.3 2008/08/11 18:44:58 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


-- Error messages resulting from qualified trigger names.
--
local function arr_match(array, str)
    for _, v in ipairs(array) do
        if type(v) == 'string' and string.find(v, str) then
            return 1
        end
    end
    return 0
end

-- When the UPDATE OF syntax is used, no code is generated for triggers
-- that do not match the update columns.
--
test:do_test(
    "trigger7-2.1",
    function()
        test:execsql [[
			CREATE TABLE t1(x INT PRIMARY KEY, y INT);
            CREATE TRIGGER r1 AFTER UPDATE OF x ON t1 BEGIN
              SELECT '___update_t1.x___';
            END;
            CREATE TRIGGER r2 AFTER UPDATE OF y ON t1 BEGIN
              SELECT '___update_t1.y___';
            END;
        ]]
        txt = test:execsql "EXPLAIN UPDATE t1 SET x=5"
        return arr_match(txt, "___update_t1.x___")
        -- return X(63, "X!cmd", [=[["string","match","*___update_t1.x___*",["txt"]]]=])
    end,
        -- <trigger7-2.1>
        1
        -- </trigger7-2.1>
    )

test:do_test(
    "trigger7-2.2",
    function()
        txt = test:execsql "EXPLAIN UPDATE t1 SET x=5"
        return arr_match(txt, "___update_t1.y___")
        -- return X(67, "X!cmd", [=[["string","match","*___update_t1.y___*",["txt"]]]=])
    end,
        -- <trigger7-2.2>
        0
        -- </trigger7-2.2>
    )

test:do_test(
    "trigger7-2.3",
    function()
        txt = test:execsql "EXPLAIN UPDATE t1 SET y=5"
        return arr_match(txt, "___update_t1.x___")
        -- return X(71, "X!cmd", [=[["string","match","*___update_t1.x___*",["txt"]]]=])
    end,
        -- <trigger7-2.3>
        0
        -- </trigger7-2.3>
    )

test:do_test(
    "trigger7-2.4",
    function()
        txt = test:execsql "EXPLAIN UPDATE t1 SET y=5"
        return arr_match(txt, "___update_t1.y___")
        -- return X(75, "X!cmd", [=[["string","match","*___update_t1.y___*",["txt"]]]=])
    end,
        -- <trigger7-2.4>
        1
        -- </trigger7-2.4>
    )

-- do_test trigger7-2.5 {
--   set txt [db eval {EXPLAIN UPDATE t1 SET =10}]
--   string match *___update_t1.x___* $txt
-- } 0
-- do_test trigger7-2.6 {
--   set txt [db eval {EXPLAIN UPDATE t1 SET x=5}]
--   string match *___update_t1.x___* $txt
-- } 0


-- Test the ability to create many triggers on the same table, then
-- selectively drop those triggers.
--
test:do_execsql_test(
    "trigger7-3.1",
    [[
        CREATE TABLE t2(x INT PRIMARY KEY,y INT,z INT);
        CREATE TRIGGER t2r1 AFTER INSERT ON t2 BEGIN SELECT 1; END;
        CREATE TRIGGER t2r2 BEFORE INSERT ON t2 BEGIN SELECT 1; END;
        CREATE TRIGGER t2r3 AFTER UPDATE ON t2 BEGIN SELECT 1; END;
        CREATE TRIGGER t2r4 BEFORE UPDATE ON t2 BEGIN SELECT 1; END;
        CREATE TRIGGER t2r5 AFTER DELETE ON t2 BEGIN SELECT 1; END;
        CREATE TRIGGER t2r6 BEFORE DELETE ON t2 BEGIN SELECT 1; END;
        CREATE TRIGGER t2r7 AFTER INSERT ON t2 BEGIN SELECT 1; END;
        CREATE TRIGGER t2r8 BEFORE INSERT ON t2 BEGIN SELECT 1; END;
        CREATE TRIGGER t2r9 AFTER UPDATE ON t2 BEGIN SELECT 1; END;
        CREATE TRIGGER t2r10 BEFORE UPDATE ON t2 BEGIN SELECT 1; END;
        CREATE TRIGGER t2r11 AFTER DELETE ON t2 BEGIN SELECT 1; END;
        CREATE TRIGGER t2r12 BEFORE DELETE ON t2 BEGIN SELECT 1; END;
        DROP TRIGGER t2r6;
    ]], {
        -- <trigger7-3.1>
        
        -- </trigger7-3.1>
    })

-- # This test corrupts the database file so it must be the last test
-- # in the series.
-- #
-- do_test trigger7-99.1 {
--   execsql {
--     UPDATE sqlite_master SET sql='nonsense';
--   }
--   db close
--   catch { sqlite3 db test.db }
--   catchsql { DROP TRIGGER t2r5 }
-- } {1 {malformed database schema (t2r12)}}
test:finish_test()
