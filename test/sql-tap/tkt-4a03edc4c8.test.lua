#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(2)

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
-- MUST_WORK_TEST
if (0 > 0)
 then
end
test:do_test(
    "tkt-4a03ed-1.1",
    function()
        test:execsql [[
            CREATE TABLE t1(
              a INTEGER PRIMARY KEY ON CONFLICT REPLACE,
              b UNIQUE ON CONFLICT FAIL
            );
            INSERT INTO t1 VALUES(1, 1);
            INSERT INTO t1 VALUES(2, 2);
        ]]
        return test:catchsql [[
            BEGIN;
              INSERT INTO t1 VALUES(1, 2);
            COMMIT;
        ]]
    end, {
        -- <tkt-4a03ed-1.1>
        1, "UNIQUE constraint failed: t1.b"
        -- </tkt-4a03ed-1.1>
    })

-- do_test tkt-4a03ed-1.2 {
--   db eval {
--     PRAGMA integrity_check;
--   }
-- } {ok}
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

test:finish_test()

