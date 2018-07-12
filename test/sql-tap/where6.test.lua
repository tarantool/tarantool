#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(20)

--!./tcltestrunner.lua
-- 2007 June 8
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
-- focus of this file is testing that terms in the ON clause of
-- a LEFT OUTER JOIN are not used with indices.  See ticket #3015.
--
-- $Id: where6.test,v 1.2 2008/04/17 19:14:02 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Build some test data
--
test:do_execsql_test(
    "where6-1.1",
    [[
        CREATE TABLE t1(a INTEGER PRIMARY KEY,b INT ,c INT );
        INSERT INTO t1 VALUES(1,3,1);
        INSERT INTO t1 VALUES(2,4,2);
        CREATE TABLE t2(x INTEGER PRIMARY KEY);
        INSERT INTO t2 VALUES(3);

        SELECT * FROM t1 LEFT JOIN t2 ON b=x AND c=1;
    ]], {
        -- <where6-1.1>
        1, 3, 1, 3, 2, 4, 2, ""
        -- </where6-1.1>
    })

test:do_execsql_test(
    "where6-1.2",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON x=b AND c=1;
    ]], {
        -- <where6-1.2>
        1, 3, 1, 3, 2, 4, 2, ""
        -- </where6-1.2>
    })

test:do_execsql_test(
    "where6-1.3",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON x=b AND 1=c;
    ]], {
        -- <where6-1.3>
        1, 3, 1, 3, 2, 4, 2, ""
        -- </where6-1.3>
    })

test:do_execsql_test(
    "where6-1.4",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON b=x AND 1=c;
    ]], {
        -- <where6-1.4>
        1, 3, 1, 3, 2, 4, 2, ""
        -- </where6-1.4>
    })

test:do_test(
    "where6-1.5",
    function()
        -- return X(55, "X!cmd", [=[["explain_no_trace","SELECT * FROM t1 LEFT JOIN t2 ON x=b AND 1=c"]]=])
        return test:explain_no_trace('SELECT * FROM t1 LEFT JOIN t2 ON x=b AND 1=c')
    end,
    -- <where6-1.5>
    -- X(54, "X!cmd", [=[["explain_no_trace","SELECT * FROM t1 LEFT JOIN t2 ON b=x AND c=1"]]=])
    test:explain_no_trace('SELECT * FROM t1 LEFT JOIN t2 ON b=x AND c=1')
    -- </where6-1.5>
    )

test:do_test(
    "where6-1.6",
    function()
        -- return X(58, "X!cmd", [=[["explain_no_trace","SELECT * FROM t1 LEFT JOIN t2 ON x=b WHERE 1=c"]]=])
        return test:explain_no_trace('SELECT * FROM t1 LEFT JOIN t2 ON x=b WHERE 1=c')
    end, 
    -- <where6-1.6>
    -- X(57, "X!cmd", [=[["explain_no_trace","SELECT * FROM t1 LEFT JOIN t2 ON b=x WHERE c=1"]]=])
    test:explain_no_trace('SELECT * FROM t1 LEFT JOIN t2 ON b=x WHERE c=1')    
    -- </where6-1.6>
    )

test:do_execsql_test(
    "where6-1.11",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON b=x WHERE c=1;
    ]], {
        -- <where6-1.11>
        1, 3, 1, 3
        -- </where6-1.11>
    })

test:do_execsql_test(
    "where6-1.12",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON x=b WHERE c=1;
    ]], {
        -- <where6-1.12>
        1, 3, 1, 3
        -- </where6-1.12>
    })

test:do_execsql_test(
    "where6-1.13",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON b=x WHERE 1=c;
    ]], {
        -- <where6-1.13>
        1, 3, 1, 3
        -- </where6-1.13>
    })

test:do_execsql_test(
    "where6-2.1",
    [[
        CREATE INDEX i1 ON t1(c);

        SELECT * FROM t1 LEFT JOIN t2 ON b=x AND c=1;
    ]], {
        -- <where6-2.1>
        1, 3, 1, 3, 2, 4, 2, ""
        -- </where6-2.1>
    })

test:do_execsql_test(
    "where6-2.2",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON x=b AND c=1;
    ]], {
        -- <where6-2.2>
        1, 3, 1, 3, 2, 4, 2, ""
        -- </where6-2.2>
    })

test:do_execsql_test(
    "where6-2.3",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON x=b AND 1=c;
    ]], {
        -- <where6-2.3>
        1, 3, 1, 3, 2, 4, 2, ""
        -- </where6-2.3>
    })

test:do_execsql_test(
    "where6-2.4",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON b=x AND 1=c;
    ]], {
        -- <where6-2.4>
        1, 3, 1, 3, 2, 4, 2, ""
        -- </where6-2.4>
    })

test:do_test(
    "where6-2.5",
    function()
        -- return X(105, "X!cmd", [=[["explain_no_trace","SELECT * FROM t1 LEFT JOIN t2 ON x=b AND 1=c"]]=])
        return test:explain_no_trace('SELECT * FROM t1 LEFT JOIN t2 ON x=b AND 1=c')
    end,
    -- <where6-2.5>
    -- X(104, "X!cmd", [=[["explain_no_trace","SELECT * FROM t1 LEFT JOIN t2 ON b=x AND c=1"]]=])
    test:explain_no_trace('SELECT * FROM t1 LEFT JOIN t2 ON b=x AND c=1')
    -- </where6-2.5>
    )

test:do_test(
    "where6-2.6",
    function()
        -- return X(108, "X!cmd", [=[["explain_no_trace","SELECT * FROM t1 LEFT JOIN t2 ON x=b WHERE 1=c"]]=])
        return test:explain_no_trace('SELECT * FROM t1 LEFT JOIN t2 ON x=b WHERE 1=c')
    end,
        -- <where6-2.6>
        -- X(107, "X!cmd", [=[["explain_no_trace","SELECT * FROM t1 LEFT JOIN t2 ON b=x WHERE c=1"]]=])
        test:explain_no_trace('SELECT * FROM t1 LEFT JOIN t2 ON b=x WHERE c=1')
        -- </where6-2.6>
    )



test:do_execsql_test(
    "where6-2.11",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON b=x WHERE c=1;
    ]], {
        -- <where6-2.11>
        1, 3, 1, 3
        -- </where6-2.11>
    })

test:do_execsql_test(
    "where6-2.12",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON x=b WHERE c=1;
    ]], {
        -- <where6-2.12>
        1, 3, 1, 3
        -- </where6-2.12>
    })

test:do_execsql_test(
    "where6-2.13",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON x=b WHERE 1=c;
    ]], {
        -- <where6-2.13>
        1, 3, 1, 3
        -- </where6-2.13>
    })

test:do_execsql_test(
    "where6-2.14",
    [[
        SELECT * FROM t1 LEFT JOIN t2 ON b=x WHERE 1=c;
    ]], {
        -- <where6-2.14>
        1, 3, 1, 3
        -- </where6-2.14>
    })

-- Ticket [ebdbadade5b]:
-- If the ON close on a LEFT JOIN is of the form x=y where both x and y
-- are indexed columns on tables to left of the join, then do not use that 
-- term with indices to either table.
--
test:do_test(
    "where6-3.1",
    function()
        return test:execsql [[
            CREATE TABLE t4(x TEXT PRIMARY key);
            INSERT INTO t4 VALUES('abc');
            INSERT INTO t4 VALUES('def');
            INSERT INTO t4 VALUES('ghi');
            CREATE TABLE t5(a TEXT, b TEXT , c INT , PRIMARY KEY(a,b));
            INSERT INTO t5 VALUES('abc','def',123);
            INSERT INTO t5 VALUES('def','ghi',456);

            SELECT t4a.x, t4b.x, t5.c, t6.v
              FROM t4 AS t4a
                   INNER JOIN t4 AS t4b
                   LEFT JOIN t5 ON t5.a=t4a.x AND t5.b=t4b.x
                   LEFT JOIN (SELECT 1 AS v) AS t6 ON t4a.x=t4b.x
             ORDER BY 1, 2, 3;
        ]]
    end, {
        -- <where6-3.1>
        "abc", "abc", "", 1, "abc", "def", 123, "", "abc", "ghi", "", "", "def", "abc", "", "", "def", "def", "", 1, "def", "ghi", 456, "", "ghi", "abc", "", "", "ghi", "def", "", "", "ghi", "ghi", "", 1
        -- </where6-3.1>
    })

test:finish_test()

