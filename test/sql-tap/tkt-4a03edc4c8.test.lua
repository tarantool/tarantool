#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(1)

--!./tcltestrunner.lua
-- 2009 September 23
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
-- This file implements tests to verify that
-- ticket [4a03edc4c8c028c93e9269f64fc5e97f632c1166] has been fixed.  
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_test(
    "tkt-4a03ed-1.1",
    function()
        test:execsql [[
            CREATE TABLE t1(
              a INTEGER PRIMARY KEY,
              b UNIQUE
            );
            INSERT INTO t1 VALUES(1, 1);
            INSERT INTO t1 VALUES(2, 2);
        ]]
        return test:catchsql [[
            START TRANSACTION;
              INSERT INTO t1 VALUES(1, 2);
            COMMIT;
        ]]
    end, {
        -- <tkt-4a03ed-1.1>
        1, "Duplicate key exists in unique index 'pk_unnamed_T1_1' in space 'T1'"
        -- </tkt-4a03ed-1.1>
    })

-- do_test tkt-4a03ed-1.2 {
--   db eval {
--     PRAGMA integrity_check;
--   }
-- } {ok}

-- this test stopped working after reordering indexes
-- I suppose it happened because order of constraint checking is changed and
-- 1) a INTEGER PRIMARY KEY ON CONFLICT REPLACE deleted record 1 1
-- 2) b UNIQUE ON CONFLICT FAIL failed
-- 3) transaction should be rolled back (and undelete 1 1) but transactions are not working
-- MUST_WORK_TEST WAITING FOR TRANSACTIONS #2140
if (0 > 0) then
test:do_test(
    "tkt-4a03ed-1.3",
    function()
        return test:execsql [[
            SELECT * FROM t1 ORDER BY a;
        ]]
    end, {
        -- <tkt-4a03ed-1.3>
        1, 1, 2, 2
        -- </tkt-4a03ed-1.3>
    })
end
test:finish_test()

