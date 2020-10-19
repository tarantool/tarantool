#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(17)

--!./tcltestrunner.lua
-- 2009 February 23
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
-- focus of this file is testing the reverse_select_order pragma.
--
-- $Id: whereA.test,v 1.3 2009/06/10 19:33:29 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_test(
    "whereA-1.1",
    function()
        return test:execsql [[
            CREATE TABLE t1(a INTEGER PRIMARY KEY, b NUMBER UNIQUE, c TEXT);
            INSERT INTO t1 VALUES(1,2,'3');
            INSERT INTO t1 values(2,55,'world');
            INSERT INTO t1 VALUES(3,4.53,NULL);
            SELECT * FROM t1
        ]]
    end, {
        -- <whereA-1.1>
        1, 2, '3', 2, 55, "world", 3, 4.53, ""
        -- </whereA-1.1>
    })

test:do_test(
    "whereA-1.2",
    function()
        return test:execsql [[
            UPDATE "_session_settings" SET "value" = true WHERE "name" = 'sql_reverse_unordered_selects';
            SELECT * FROM t1;
        ]]
    end, {
        -- <whereA-1.2>
        3, 4.53, "", 2, 55, "world", 1, 2, '3'
        -- </whereA-1.2>
    })

-- MUST_WORK_TEST
test:do_test(
    "whereA-1.3",
    function()
        --db close
        --sql db test.db
        return test:execsql [[
            UPDATE "_session_settings" SET "value" = true WHERE "name" = 'sql_reverse_unordered_selects';
            SELECT * FROM t1;
        ]]
    end, {
        -- <whereA-1.3>
        3, 4.53, "", 2, 55, "world", 1, 2, '3'
        -- </whereA-1.3>
    })

-- do_test whereA-1.4 {
--   db close
--   sql db test.db
--   db eval {
--     PRAGMA reverse_unordered_selects=1;
--     SELECT * FROM t1 ORDER BY rowid;
--   }
-- } {1 2 3 2 hello world 3 4.53 {}}
test:do_test(
    "whereA-1.6",
    function()
        return box.space._session_settings:get('sql_reverse_unordered_selects').value
    end,
        -- <whereA-1.6>
        true
        -- </whereA-1.6>
    )

test:do_execsql_test(
    "whereA-1.8",
    [[
        SELECT * FROM t1 WHERE b=2 AND a IS NULL;
    ]], {
        -- <whereA-1.8>
        
        -- </whereA-1.8>
    })

test:do_execsql_test(
    "whereA-1.9",
    [[
        SELECT * FROM t1 WHERE b=2 AND a IS NOT NULL;
    ]], {
        -- <whereA-1.9>
        1, 2, '3'
        -- </whereA-1.9>
    })

test:do_test(
    "whereA-2.1",
    function()
        return test:execsql [[
            UPDATE "_session_settings" SET "value" = false WHERE "name" = 'sql_reverse_unordered_selects';
            SELECT * FROM t1 WHERE a>0;
        ]]
    end, {
        -- <whereA-2.1>
        1, 2, '3', 2, 55, "world", 3, 4.53, ""
        -- </whereA-2.1>
    })

test:do_test(
    "whereA-2.2",
    function()
        return test:execsql [[
            UPDATE "_session_settings" SET "value" = true WHERE "name" = 'sql_reverse_unordered_selects';
            SELECT * FROM t1 WHERE a>0;
        ]]
    end, {
        -- <whereA-2.2>
        3, 4.53, "", 2, 55, "world", 1, 2, '3'
        -- </whereA-2.2>
    })

-- do_test whereA-2.3 {
--   db eval {
--     PRAGMA reverse_unordered_selects=1;
--     SELECT * FROM t1 WHERE a>0 ORDER BY rowid;
--   }
-- } {1 2 3 2 hello world 3 4.53 {}}
test:do_test(
    "whe:reA-3.1",
    function()
        return test:execsql [[
            UPDATE "_session_settings" SET "value" = false WHERE "name" = 'sql_reverse_unordered_selects';
            SELECT * FROM t1 WHERE b>0;
        ]]
    end, {
        -- <whereA-3.1>
        1, 2, '3', 3, 4.53, "", 2, 55, "world"
        -- </whereA-3.1>
    })

test:do_test(
    "whereA-3.2",
    function()
        return test:execsql [[
            UPDATE "_session_settings" SET "value" = true WHERE "name" = 'sql_reverse_unordered_selects';
            SELECT * FROM t1 WHERE b>0;
        ]]
    end, {
        -- <whereA-3.2>
        2, 55, "world", 3, 4.53, "", 1, 2, '3'
        -- </whereA-3.2>
    })

test:do_test(
    "whereA-3.3",
    function()
        return test:execsql [[
            UPDATE "_session_settings" SET "value" = true WHERE "name" = 'sql_reverse_unordered_selects';
            SELECT * FROM t1 WHERE b>0 ORDER BY b;
        ]]
    end, {
        -- <whereA-3.3>
        1, 2, '3', 3, 4.53, "", 2, 55, "world"
        -- </whereA-3.3>
    })

test:do_test(
    "whereA-4.1",
    function()
        return test:execsql [[
            CREATE TABLE t2(id int primary key, x INT);
            INSERT INTO t2 VALUES(1, 1);
            INSERT INTO t2 VALUES(2, 2);
            SELECT x FROM t2;
        ]]
    end, {
        -- <whereA-4.1>
        2, 1
        -- </whereA-4.1>
    })

-- Do an SQL statement.  Append the search count to the end of the result.
--
local function count(sql)
    local sql_sort_count = box.stat.sql().sql_sort_count
    local r = test:execsql(sql)
    table.insert(r, box.stat.sql().sql_sort_count - sql_sort_count)
    return r
end

test:do_test(
    "whereA-4.2",
    function()
        -- Ticket #3904
        test:execsql([[
            CREATE INDEX t2x ON t2(x);
        ]])
        return count([[
    SELECT x FROM t2;
  ]])
    end, {
    -- <whereA-4.2>
    2, 1, 0
    -- </whereA-4.2>
})

test:do_test(
    "whereA-4.3",
    function()
        return count([[
    SELECT x FROM t2 ORDER BY x;
  ]])
    end, {
    -- <whereA-4.3>
    1, 2, 0
    -- </whereA-4.3>
})

test:do_test(
    "whereA-4.4",
    function()
        return count([[
    SELECT x FROM t2 ORDER BY x DESC;
  ]])
    end, {
    -- <whereA-4.4>
    2, 1, 0
    -- </whereA-4.4>
})

test:do_test(
    "whereA-4.5",
    function()
        test:execsql("DROP INDEX t2x ON t2;")
        return count([[
    SELECT x FROM t2 ORDER BY x;
  ]])
    end, {
    -- <whereA-4.5>
    1, 2, 1
    -- </whereA-4.5>
})

test:do_test(
    "whereA-4.6",
    function()
        return count([[
    SELECT x FROM t2 ORDER BY x DESC;
  ]])
    end, {
    -- <whereA-4.6>
    2, 1, 1
    -- </whereA-4.6>
})

test:finish_test()

