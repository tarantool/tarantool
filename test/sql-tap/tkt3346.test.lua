#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(5)

--!./tcltestrunner.lua
-- 2008 September 1
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
--
-- This file implements regression tests for sql library.  The
-- focus of this file is testing the fix for ticket #3346
--
-- $Id: tkt3346.test,v 1.3 2008/12/09 13:12:57 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_test(
    "tkt3346-1.1",
    function()
        return test:execsql [[
            CREATE TABLE t1(id  INT primary key, a INT ,b TEXT);
            INSERT INTO t1 VALUES(1, 2,'bob');
            INSERT INTO t1 VALUES(2, 1,'alice');
            INSERT INTO t1 VALUES(3, 3,'claire');
            SELECT *, ( SELECT y FROM (SELECT x.b='alice' AS y) )
              FROM ( SELECT a,b FROM t1 ) AS x;
        ]]
    end, {
        -- <tkt3346-1.1>
        2, "bob", false, 1, "alice", true, 3, "claire", false
        -- </tkt3346-1.1>
    })

test:do_test(
    "tkt3346-1.2",
    function()
        return test:execsql [[
            SELECT b FROM (SELECT a,b FROM t1) AS x
             WHERE (SELECT y FROM (SELECT x.b='alice' AS y))=false
        ]]
    end, {
        -- <tkt3346-1.2>
        "bob", "claire"
        -- </tkt3346-1.2>
    })

test:do_test(
    "tkt3346-1.3",
    function()
        return test:execsql [[
            SELECT b FROM (SELECT a,b FROM t1 ORDER BY a) AS x
             WHERE (SELECT y FROM (SELECT CAST(a AS TEXT)||b y FROM t1 WHERE t1.b=x.b))=(CAST(x.a AS TEXT)||x.b)
        ]]
    end, {
        -- <tkt3346-1.3>
        "alice", "bob", "claire"
        -- </tkt3346-1.3>
    })

test:do_test(
    "tkt3346-1.4",
    function()
        return test:execsql [[
            SELECT b FROM (SELECT a,b FROM t1 ORDER BY a) AS x
             WHERE (SELECT y FROM (SELECT CAST(a AS TEXT)||b y FROM t1 WHERE t1.b=x.b))=('2'||x.b)
        ]]
    end, {
        -- <tkt3346-1.4>
        "bob"
        -- </tkt3346-1.4>
    })

-- Ticket #3530
--
-- As shown by ticket #3346 above (see also ticket #3298) it is important
-- that a subquery in the result-set be able to look up through multiple
-- FROM levels in order to view tables in the FROM clause at the top level.
--
-- But ticket #3530 shows us that a subquery in the FROM clause should not
-- be able to look up to higher levels:
--
test:do_catchsql_test(
    "tkt3346-2.1",
    [[
        CREATE TABLE t2(a  INT primary key);
        INSERT INTO t2 VALUES(1);

        SELECT * FROM (SELECT a,b FROM t1 WHERE 1=x.a) AS x;
    ]], {
        -- <tkt3346-2.1>
        1, "Field 'A' was not found in space 'X' format"
        -- </tkt3346-2.1>
    })

test:finish_test()

