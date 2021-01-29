#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(60)

--!./tcltestrunner.lua
-- 2001 September 23
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library.  The
-- focus of this file is stressing the library by putting large amounts
-- of data in a single row of a table.
--
-- $Id: bigrow.test,v 1.5 2004/08/07 23:54:48 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Make a big string that we can use for test data
--
local bigstr = ""
test:do_test(
    "bigrow-1.0",
    function()
        local alphabet = "abcdefghijklmnopqrstuvwxyz"
        for i = 1, 9999, 1 do
            local sep = string.sub(alphabet, i%26 + 1,i%26 + 1)
            bigstr = bigstr .. string.format("%s %s ", sep, string.format("%04d",i))
        end
        return string.len(bigstr)
    end,
        69993)

-- Make a table into which we can insert some but records.
--
test:do_execsql_test(
    "bigrow-1.1",
    [[
        CREATE TABLE t1(id INT PRIMARY KEY AUTOINCREMENT, a TEXT UNIQUE, b TEXT, c TEXT);
        --SELECT name FROM sql_master
        --  WHERE type='table' OR type='index'
        --  ORDER BY name
    ]], {
        -- <bigrow-1.1>

        -- </bigrow-1.1>
    })

local big1
test:do_test(
    "bigrow-1.2",
    function()
        big1 = string.sub(bigstr, 1, 65519 + 1)
        local sql = "INSERT INTO t1(a,b,c) VALUES('abc',"
        sql = sql .. "'"..big1.."', 'xyz');"
        test:execsql(sql)
        return test:execsql "SELECT a, c FROM t1"
    end, {
        -- <bigrow-1.2>
        "abc", "xyz"
        -- </bigrow-1.2>
    })

test:do_execsql_test(
    "bigrow-1.3",
    [[
        SELECT b FROM t1
    ]], {
        -- <bigrow-1.3>
        big1
        -- </bigrow-1.3>
    })

local big2
test:do_test(
    "bigrow-1.4",
    function()
        big2 = string.sub(bigstr, 1, 65520+1)
        local sql = "INSERT INTO t1(a,b,c) VALUES('abc2',"
        sql = sql .. "'"..big2.."', 'xyz2');"
        return test:catchsql(sql)
    end, {
        -- <bigrow-1.4>
        0,
        -- </bigrow-1.4>
    })

test:do_execsql_test(
    "bigrow-1.4.1",
    [[
        SELECT b FROM t1 ORDER BY c
    ]], {
        -- <bigrow-1.4.1>
        big1, big2
        -- </bigrow-1.4.1>
    })

test:do_execsql_test(
    "bigrow-1.4.2",
    [[
        SELECT c FROM t1 ORDER BY c
    ]], {
        -- <bigrow-1.4.2>
        "xyz", "xyz2"
        -- </bigrow-1.4.2>
    })

test:do_test(
    "bigrow-1.4.3",
    function()
        test:execsql "DELETE FROM t1 WHERE a='abc2'"
        return test:execsql "SELECT c FROM t1"
    end, {
        -- <bigrow-1.4.3>
        "xyz"
        -- </bigrow-1.4.3>
    })

test:do_execsql_test(
    "bigrow-1.5",
    [[
        UPDATE t1 SET a=b, b=a;
        SELECT b,c FROM t1
    ]], {
        -- <bigrow-1.5>
        "abc", "xyz"
        -- </bigrow-1.5>
    })

test:do_execsql_test(
    "bigrow-1.6",
    [[
        SELECT a,b,c FROM t1
    ]], {
        -- <bigrow-1.6>
        big1, "abc", "xyz"
        -- </bigrow-1.6>
    })

test:do_execsql_test(
    "bigrow-1.7",
    [[
        INSERT INTO t1(a,b,c) VALUES('1','2','3');
        INSERT INTO t1(a,b,c) VALUES('A','B','C');
        SELECT b FROM t1 WHERE a=='1';
    ]], {
        -- <bigrow-1.7>
        "2"
        -- </bigrow-1.7>
    })

test:do_execsql_test(
    "bigrow-1.8",
    "SELECT b FROM t1 WHERE a=='"..big1.."'", {
        -- <bigrow-1.8>
        "abc"
        -- </bigrow-1.8>
    })

test:do_execsql_test(
    "bigrow-1.9",
    "SELECT b FROM t1 WHERE a!='"..big1.."' ORDER BY a", {
        -- <bigrow-1.9>
        "2", "B"
        -- </bigrow-1.9>
    })

-- Try doing some indexing on big columns
--
test:do_test(
    "bigrow-2.1",
    function()
        test:execsql [[
            CREATE INDEX i1 ON t1(a)
        ]]
        return test:execsql("SELECT b FROM t1 WHERE a=='"..big1.."'")
    end, {
        -- <bigrow-2.1>
        "abc"
        -- </bigrow-2.1>
    })

test:do_test(
    "bigrow-2.2",
    function()
        test:execsql [[
            UPDATE t1 SET a=b, b=a
        ]]
        return test:execsql "SELECT b FROM t1 WHERE a=='abc'"
    end, {
        -- <bigrow-2.2>
        big1
        -- </bigrow-2.2>
    })

test:do_test(
    "bigrow-2.3",
    function()
        test:execsql [[
            UPDATE t1 SET a=b, b=a
        ]]
        return test:execsql("SELECT b FROM t1 WHERE a=='"..big1.."'")
    end, {
        -- <bigrow-2.3>
        "abc"
        -- </bigrow-2.3>
    })

-- Mosts of the tests above were created back when rows were limited in
-- size to 64K.  Now rows can be much bigger.  Test that logic.  Also
-- make sure things work correctly at the transition boundries between
-- row sizes of 256 to 257 bytes and from 65536 to 65537 bytes.
--
-- We begin by testing the 256..257 transition.
--
test:do_test(
    "bigrow-3.1",
    function()
        test:execsql [[
            DELETE FROM t1;
            INSERT INTO t1(a,b,c) VALUES('one','abcdefghijklmnopqrstuvwxyz0123','hi');
        ]]
        return test:execsql "SELECT a,length(b),c FROM t1"
    end, {
        -- <bigrow-3.1>
        "one", 30, "hi"
        -- </bigrow-3.1>
    })

test:do_test(
    "bigrow-3.2",
    function()
        test:execsql [[
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
        ]]
        return test:execsql "SELECT a,length(b),c FROM t1"
    end, {
        -- <bigrow-3.2>
        "one", 240, "hi"
        -- </bigrow-3.2>
    })

for i = 1, 9, 1 do
    test:do_test(
        "bigrow-3.3."..i,
        function()
            test:execsql("UPDATE t1 SET b=b||'"..i.."'")
            return test:execsql "SELECT a,length(b),c FROM t1"
        end, {
            "one", 240 + i, "hi"
        })

end
-- Now test the 65536..65537 row-size transition.
--
test:do_test(
    "bigrow-4.1",
    function()
        test:execsql [[
            DELETE FROM t1;
            INSERT INTO t1(a,b,c) VALUES('one','abcdefghijklmnopqrstuvwxyz0123','hi');
        ]]
        return test:execsql "SELECT a,length(b),c FROM t1"
    end, {
        -- <bigrow-4.1>
        "one", 30, "hi"
        -- </bigrow-4.1>
    })

test:do_test(
    "bigrow-4.2",
    function()
        test:execsql [[
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
            UPDATE t1 SET b=b||b;
        ]]
        return test:execsql "SELECT a,length(b),c FROM t1"
    end, {
        -- <bigrow-4.2>
        "one", 122880, "hi"
        -- </bigrow-4.2>
    })

test:do_test(
    "bigrow-4.3",
    function()
        test:execsql [[
            UPDATE t1 SET b=substr(b,1,65515)
        ]]
        return test:execsql "SELECT a,length(b),c FROM t1"
    end, {
        -- <bigrow-4.3>
        "one", 65515, "hi"
        -- </bigrow-4.3>
    })

for i = 1, 9, 1 do
    test:do_test(
        "bigrow-4.4."..i,
        function()
            test:execsql("UPDATE t1 SET b=b||'"..i.."'")
            return test:execsql "SELECT a,length(b),c FROM t1"
        end, {
            "one", 65515 + i, "hi"
        })

end
-- Check to make sure the library recovers safely if a row contains
-- too much data.
--
test:do_test(
    "bigrow-5.1",
    function()
        test:execsql [[
            DELETE FROM t1;
            INSERT INTO t1(a,b,c) VALUES('one','abcdefghijklmnopqrstuvwxyz0123','hi');
        ]]
        return test:execsql "SELECT a,length(b),c FROM t1"
    end, {
        -- <bigrow-5.1>
        "one", 30, "hi"
        -- </bigrow-5.1>
    })

local i = 1
local sz = 60
while sz < 1048560 -1 do
    test:do_execsql_test(
        "bigrow-5.2."..i,
        [[
            UPDATE t1 SET b=b||b;
            SELECT a,length(b),c FROM t1;
        ]], {
            "one", sz, "hi"
        })
    sz = sz + sz
    i = i + 1
end
test:do_catchsql_test(
    "bigrow-5.3",
    [[
        UPDATE t1 SET b=b||b
    ]], {
        -- <bigrow-5.3>
        0,
        -- </bigrow-5.3>
    })

test:do_execsql_test(
    "bigrow-5.4",
    [[
        SELECT length(b) FROM t1
    ]], {
        -- <bigrow-5.4>
        1966080
        -- </bigrow-5.4>
    })

test:do_catchsql_test(
    "bigrow-5.5",
    [[
        UPDATE t1 SET b=b||b
    ]], {
        -- <bigrow-5.5>
        0,
        -- </bigrow-5.5>
    })

test:do_execsql_test(
    "bigrow-5.6",
    [[
        SELECT length(b) FROM t1
    ]], {
        -- <bigrow-5.6>
        3932160
        -- </bigrow-5.6>
    })

test:do_execsql_test(
    "bigrow-5.99",
    [[
        DROP TABLE t1
    ]], {
        -- <bigrow-5.99>

        -- </bigrow-5.99>
    })



test:finish_test()
