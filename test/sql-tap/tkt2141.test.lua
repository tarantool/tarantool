#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(3)

--!./tcltestrunner.lua
-- 2007 January 03
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
-- This file implements tests to verify that ticket #2141 has been
-- fixed.  
--
--
-- $Id: tkt2141.test,v 1.2 2007/09/12 17:01:45 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


test:do_execsql_test(
    "tkt2141-1.1",
    [[
        CREATE TABLE tab1 (t1_id integer PRIMARY KEY, t1_desc TEXT);
        INSERT INTO tab1 VALUES(1,'rec 1 tab 1');
        CREATE TABLE tab2 (t2_id integer PRIMARY KEY, t2_id_t1 INT , t2_desc TEXT);
        INSERT INTO tab2 VALUES(1,1,'rec 1 tab 2');
        CREATE TABLE tab3 (t3_id integer PRIMARY KEY, t3_id_t2 INT , t3_desc TEXT);
        INSERT INTO tab3 VALUES(1,1,'aa');
        SELECT *
        FROM tab1 t1 LEFT JOIN tab2 t2 ON t1.t1_id = t2.t2_id_t1
        WHERE t2.t2_id IN
             (SELECT t2_id FROM tab2, tab3 ON t2_id = t3_id_t2
               WHERE t3_id IN (1,2) GROUP BY t2_id);
    ]], {
        -- <tkt2141-1.1>
        1, "rec 1 tab 1", 1, 1, "rec 1 tab 2"
        -- </tkt2141-1.1>
    })

test:do_execsql_test(
    "tkt2141-1.2",
    [[
        SELECT *
        FROM tab1 t1 LEFT JOIN tab2 t2 ON t1.t1_id = t2.t2_id_t1
        WHERE t2.t2_id IN
             (SELECT t2_id FROM tab2, tab3 ON t2_id = t3_id_t2
               WHERE t3_id IN (1,2));
    ]], {
        -- <tkt2141-1.2>
        1, "rec 1 tab 1", 1, 1, "rec 1 tab 2"
        -- </tkt2141-1.2>
    })

test:do_execsql_test(
    "tkt2141-1.3",
    [[
        SELECT *
        FROM tab1 t1 LEFT JOIN tab2 t2
        WHERE t2.t2_id IN
             (SELECT t2_id FROM tab2, tab3 ON t2_id = t3_id_t2
               WHERE t3_id IN (1,2));
    ]], {
        -- <tkt2141-1.3>
        1, "rec 1 tab 1", 1, 1, "rec 1 tab 2"
        -- </tkt2141-1.3>
    })

test:finish_test()

