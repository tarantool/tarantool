#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(4)

--!./tcltestrunner.lua
-- 2008 December 24
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
-- This file implements tests to verify that ticket #3554 has been
-- fixed.
--
-- $Id: tkt3554.test,v 1.2 2009/06/05 17:09:12 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


test:do_execsql_test(
    "tkt3544-1.1",
    [[
        CREATE TABLE test (id INT PRIMARY KEY AUTOINCREMENT, obj TEXT, t1 INT , t2 INT);
        CREATE UNIQUE INDEX testi1 ON test(obj, t1, t2);

        CREATE TRIGGER test_insert BEFORE INSERT ON test FOR EACH ROW BEGIN
          UPDATE test SET t1 = new.t1
            WHERE obj = new.obj AND new.t1 < t1 AND new.t2 >= t1;

          UPDATE test SET t2 = new.t2
            WHERE obj = new.obj AND new.t2 > t2 AND new.t1 <= t2;

          SELECT RAISE(IGNORE) WHERE EXISTS (
            SELECT obj FROM test
            WHERE obj = new.obj AND new.t1 >= t1 AND new.t2 <= t2
          );
        END;
    ]], {
        -- <tkt3544-1.1>

        -- </tkt3544-1.1>
    })

test:do_execsql_test(
    "tkt3544-1.2",
    [[
        INSERT INTO test(obj, t1, t2) VALUES('a', 10000, 11000);
        SELECT obj, t1, t2 FROM test;
    ]], {
        -- <tkt3544-1.2>
        "a", 10000, 11000
        -- </tkt3544-1.2>
    })

test:do_test(
    "tkt3544-1.3",
    function()
        test:execsql [[
            INSERT INTO test(obj, t1, t2) VALUES('a', 9000, 10500);
        ]]
        return test:execsql " SELECT obj, t1, t2 FROM test "
    end, {
        -- <tkt3544-1.3>
        "a", 9000, 11000
        -- </tkt3544-1.3>
    })

test:do_test(
    "tkt3544-1.4",
    function()
        test:execsql [[
            INSERT INTO test(obj, t1, t2) VALUES('a', 10000, 12000);
        ]]
        return test:execsql " SELECT obj, t1, t2 FROM test "
    end, {
        -- <tkt3544-1.4>
        "a", 9000, 12000
        -- </tkt3544-1.4>
    })

test:finish_test()

