#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(22)

--!./tcltestrunner.lua
-- 2013-12-17
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
-- focus of this file is testing the printf() SQL function.
--
--
-- EVIDENCE-OF: R-63057-40065 The printf(FORMAT,...) SQL function works
-- like the sql_mprintf() C-language function and the printf()
-- function from the standard C library.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- EVIDENCE-OF: R-40086-60101 If the FORMAT argument is missing or NULL
-- then the result is NULL.
--
test:do_execsql_test(
    "printf2-1.1",
    [[
        SELECT quote(printf()), quote(printf(NULL,1,2,3));
    ]], {
        -- <printf2-1.1>
        "NULL", "NULL"
        -- </printf2-1.1>
    })

test:do_execsql_test(
    "printf2-1.2",
    [[
        SELECT printf('hello');
    ]], {
        -- <printf2-1.2>
        "hello"
        -- </printf2-1.2>
    })

test:do_execsql_test(
    "printf2-1.3",
    [[
        SELECT printf('%d,%d,%d',55,-11,3421);
    ]], {
        -- <printf2-1.3>
        "55,-11,3421"
        -- </printf2-1.3>
    })

test:do_execsql_test(
    "printf2-1.4",
    [[
        SELECT printf('%d,%d,%d',55,'-11',3421);
    ]], {
        -- <printf2-1.4>
        "55,-11,3421"
        -- </printf2-1.4>
    })

test:do_execsql_test(
    "printf2-1.5",
    [[
        SELECT printf('%d,%d,%d,%d',55,'-11',3421);
    ]], {
        -- <printf2-1.5>
        "55,-11,3421,0"
        -- </printf2-1.5>
    })

test:do_execsql_test(
    "printf2-1.6",
    [[
        SELECT printf('%.2f',3.141592653);
    ]], {
        -- <printf2-1.6>
        "3.14"
        -- </printf2-1.6>
    })

test:do_execsql_test(
    "printf2-1.7",
    [[
        SELECT printf('%.*f',2,3.141592653);
    ]], {
        -- <printf2-1.7>
        "3.14"
        -- </printf2-1.7>
    })

test:do_execsql_test(
    "printf2-1.8",
    [[
        SELECT printf('%*.*f',5,2,3.141592653);
    ]], {
        -- <printf2-1.8>
         " 3.14"
        -- </printf2-1.8>
    })

test:do_execsql_test(
    "printf2-1.9",
    [[
        SELECT printf('%d',314159.2653);
    ]], {
        -- <printf2-1.9>
        "314159"
        -- </printf2-1.9>
    })

test:do_execsql_test(
    "printf2-1.10",
    [[
        SELECT printf('%lld',314159.2653);
    ]], {
        -- <printf2-1.10>
        "314159"
        -- </printf2-1.10>
    })

test:do_execsql_test(
    "printf2-1.11",
    [[
        SELECT printf('%lld%n',314159.2653,'hi');
    ]], {
        -- <printf2-1.11>
        "314159"
        -- </printf2-1.11>
    })

test:do_execsql_test(
    "printf2-1.12",
    [[
        SELECT printf('%n',0);
    ]], {
        -- <printf2-1.12>
        ""
        -- </printf2-1.12>
    })

-- EVIDENCE-OF: R-17002-27534 The %z format is interchangeable with %s.
--
test:do_execsql_test(
    "printf2-1.12",
    [[
        SELECT printf('%.*z',5,'abcdefghijklmnop');
    ]], {
        -- <printf2-1.12>
        "abcde"
        -- </printf2-1.12>
    })

test:do_execsql_test(
    "printf2-1.13",
    [[
        SELECT printf('%c','abcdefghijklmnop');
    ]], {
        -- <printf2-1.13>
        "a"
        -- </printf2-1.13>
    })

-- EVIDENCE-OF: R-02347-27622 The %n format is silently ignored and does
-- not consume an argument.
--
test:do_execsql_test(
    "printf2-2.1",
    [[
        CREATE TABLE t1(id INT primary key, a INT,b INT,c INT);
        INSERT INTO t1 VALUES(1, 1,2,3);
        INSERT INTO t1 VALUES(2, -1,-2,-3);
        SELECT printf('(%s)-%n-(%s)',a,b,c) FROM t1 ORDER BY id;
    ]], {
        -- <printf2-2.1>
        "(1)--(2)", "(-1)--(-2)"
        -- </printf2-2.1>
    })

-- EVIDENCE-OF: R-56064-04001 The %p format is an alias for %X.
--
test:do_execsql_test(
    "printf2-2.2",
    [[
        SELECT printf('%s=(%p)',a,a) FROM t1 ORDER BY a;
    ]], {
        -- <printf2-2.2>
        "-1=(FFFFFFFFFFFFFFFF)", "1=(1)"
        -- </printf2-2.2>
    })

-- EVIDENCE-OF: R-29410-53018 If there are too few arguments in the
-- argument list, missing arguments are assumed to have a NULL value,
-- which is translated into 0 or 0.0 for numeric formats or an empty
-- string for %s.
--
test:do_execsql_test(
    "printf2-2.3",
    [[
        SELECT printf('%s=(%d/%g/%s)',a) FROM t1 ORDER BY a;
    ]], {
        -- <printf2-2.3>
        "-1=(0/0/)", "1=(0/0/)"
        -- </printf2-2.3>
    })

-- The precision of the %c conversion causes the character to repeat.
--
test:do_execsql_test(
    "printf2-3.1",
    [[
        SELECT printf('|%110.100c|','*');
    ]], {
        -- <printf2-3.1>
        "|          ****************************************************************************************************|"
        -- </printf2-3.1>
    })

test:do_execsql_test(
    "printf2-3.2",
    [[
        SELECT printf('|%-110.100c|','*');
    ]], {
        -- <printf2-3.2>
        "|****************************************************************************************************          |"
        -- </printf2-3.2>
    })

test:do_execsql_test(
    "printf2-3.3",
    [[
        SELECT printf('|%9.8c|%-9.8c|','*','*');
    ]], {
        -- <printf2-3.3>
        "| ********|******** |"
        -- </printf2-3.3>
    })

test:do_execsql_test(
    "printf2-3.4",
    [[
        SELECT printf('|%8.8c|%-8.8c|','*','*');
    ]], {
        -- <printf2-3.4>
        "|********|********|"
        -- </printf2-3.4>
    })

test:do_execsql_test(
    "printf2-3.5",
    [[
        SELECT printf('|%7.8c|%-7.8c|','*','*');
    ]], {
        -- <printf2-3.5>
        "|********|********|"
        -- </printf2-3.5>
    })



test:finish_test()
