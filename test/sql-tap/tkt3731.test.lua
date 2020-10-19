#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(3)

--!./tcltestrunner.lua
-- 2009 March 18
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
-- $Id: tkt3731.test,v 1.1 2009/03/17 22:33:01 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


-- The tests in this file were written before sql supported recursive
-- trigger invocation, and some tests depend on that to pass. So disable
-- recursive triggers for this file.
box.space._session_settings:update('sql_recursive_triggers', {{'=', 2, false}})
test:do_execsql_test(
    "tkt3731-1.1",
    [[
        CREATE TABLE t1(a  TEXT PRIMARY KEY, b TEXT );
        CREATE TRIGGER tr1 AFTER INSERT ON t1 FOR EACH ROW BEGIN
          INSERT INTO t1 VALUES(new.a || '+', new.b || '+');
        END;
    ]], {
        -- <tkt3731-1.1>

        -- </tkt3731-1.1>
    })

test:do_execsql_test(
    "tkt3731-1.2",
    [[
        INSERT INTO t1 VALUES('a', 'b');
        INSERT INTO t1 VALUES('c', 'd');
        SELECT * FROM t1;
    ]], {
        -- <tkt3731-1.2>
        "a", "b", "a+", "b+", "c", "d", "c+", "d+"
        -- </tkt3731-1.2>
    })

test:do_execsql_test(
    "tkt3731-1.3",
    [[
        DELETE FROM t1;
        CREATE TABLE t2(a TEXT primary key, b TEXT);
        INSERT INTO t2 VALUES('e', 'f');
        INSERT INTO t2 VALUES('g', 'h');
        INSERT INTO t1 SELECT * FROM t2;
        SELECT * FROM t1;
    ]], {
        -- <tkt3731-1.3>
        "e", "f", "e+", "f+", "g", "h", "g+", "h+"
        -- </tkt3731-1.3>
    })

test:finish_test()

