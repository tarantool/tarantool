#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(130)

--!./tcltestrunner.lua
-- 2009 November 11
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
-- focus of this file is testing built-in functions.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Test plan:
--
--   func2-1.*: substr implementation (ascii)
--   func2-2.*: substr implementation (utf8)
--   func2-3.*: substr implementation (blob)
--
local function bin_to_hex(blob)
    return (blob:gsub('.', function (c)
        return string.format('%02X', string.byte(c))
    end))
end

------------------------------------------------------------------------------
-- Test cases func2-1.*: substr implementation (ascii)
--
test:do_execsql_test(
    "func2-1.1",
    [[
        SELECT 'Supercalifragilisticexpialidocious'
    ]], {
        -- <func2-1.1>
        "Supercalifragilisticexpialidocious"
        -- </func2-1.1>
    })

-- substr(x,y), substr(x,y,z)
test:do_catchsql_test(
    "func2-1.2.1",
    [[
        SELECT SUBSTR()
    ]], {
        -- <func2-1.2.1>
        1, "wrong number of arguments to function SUBSTR()"
        -- </func2-1.2.1>
    })

test:do_catchsql_test(
    "func2-1.2.2",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious')
    ]], {
        -- <func2-1.2.2>
        1, "wrong number of arguments to function SUBSTR()"
        -- </func2-1.2.2>
    })

test:do_catchsql_test(
    "func2-1.2.3",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 1,1,1)
    ]], {
        -- <func2-1.2.3>
        1, "wrong number of arguments to function SUBSTR()"
        -- </func2-1.2.3>
    })

-- p1 is 1-indexed
test:do_execsql_test(
    "func2-1.3",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 0)
    ]], {
        -- <func2-1.3>
        "Supercalifragilisticexpialidocious"
        -- </func2-1.3>
    })

test:do_execsql_test(
    "func2-1.4",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 1)
    ]], {
        -- <func2-1.4>
        "Supercalifragilisticexpialidocious"
        -- </func2-1.4>
    })

test:do_execsql_test(
    "func2-1.5",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 2)
    ]], {
        -- <func2-1.5>
        "upercalifragilisticexpialidocious"
        -- </func2-1.5>
    })

test:do_execsql_test(
    "func2-1.6",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 30)
    ]], {
        -- <func2-1.6>
        "cious"
        -- </func2-1.6>
    })

test:do_execsql_test(
    "func2-1.7",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 34)
    ]], {
        -- <func2-1.7>
        "s"
        -- </func2-1.7>
    })

test:do_execsql_test(
    "func2-1.8",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 35)
    ]], {
        -- <func2-1.8>
        ""
        -- </func2-1.8>
    })

test:do_execsql_test(
    "func2-1.9",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 36)
    ]], {
        -- <func2-1.9>
        ""
        -- </func2-1.9>
    })

-- if p1<0, start from right
test:do_execsql_test(
    "func2-1.10",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -0)
    ]], {
        -- <func2-1.10>
        "Supercalifragilisticexpialidocious"
        -- </func2-1.10>
    })

test:do_execsql_test(
    "func2-1.11",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -1)
    ]], {
        -- <func2-1.11>
        "s"
        -- </func2-1.11>
    })

test:do_execsql_test(
    "func2-1.12",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -2)
    ]], {
        -- <func2-1.12>
        "us"
        -- </func2-1.12>
    })

test:do_execsql_test(
    "func2-1.13",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -30)
    ]], {
        -- <func2-1.13>
        "rcalifragilisticexpialidocious"
        -- </func2-1.13>
    })

test:do_execsql_test(
    "func2-1.14",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -34)
    ]], {
        -- <func2-1.14>
        "Supercalifragilisticexpialidocious"
        -- </func2-1.14>
    })

test:do_execsql_test(
    "func2-1.15",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -35)
    ]], {
        -- <func2-1.15>
        "Supercalifragilisticexpialidocious"
        -- </func2-1.15>
    })

test:do_execsql_test(
    "func2-1.16",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -36)
    ]], {
        -- <func2-1.16>
        "Supercalifragilisticexpialidocious"
        -- </func2-1.16>
    })

-- p1 is 1-indexed, p2 length to return
test:do_execsql_test(
    "func2-1.17.1",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 0, 1)
    ]], {
        -- <func2-1.17.1>
        ""
        -- </func2-1.17.1>
    })

test:do_execsql_test(
    "func2-1.17.2",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 0, 2)
    ]], {
        -- <func2-1.17.2>
        "S"
        -- </func2-1.17.2>
    })

test:do_execsql_test(
    "func2-1.18",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 1, 1)
    ]], {
        -- <func2-1.18>
        "S"
        -- </func2-1.18>
    })

test:do_execsql_test(
    "func2-1.19.0",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 2, 0)
    ]], {
        -- <func2-1.19.0>
        ""
        -- </func2-1.19.0>
    })

test:do_execsql_test(
    "func2-1.19.1",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 2, 1)
    ]], {
        -- <func2-1.19.1>
        "u"
        -- </func2-1.19.1>
    })

test:do_execsql_test(
    "func2-1.19.2",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 2, 2)
    ]], {
        -- <func2-1.19.2>
        "up"
        -- </func2-1.19.2>
    })

test:do_execsql_test(
    "func2-1.20",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 30, 1)
    ]], {
        -- <func2-1.20>
        "c"
        -- </func2-1.20>
    })

test:do_execsql_test(
    "func2-1.21",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 34, 1)
    ]], {
        -- <func2-1.21>
        "s"
        -- </func2-1.21>
    })

test:do_execsql_test(
    "func2-1.22",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 35, 1)
    ]], {
        -- <func2-1.22>
        ""
        -- </func2-1.22>
    })

test:do_execsql_test(
    "func2-1.23",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 36, 1)
    ]], {
        -- <func2-1.23>
        ""
        -- </func2-1.23>
    })

-- if p1<0, start from right, p2 length to return
test:do_execsql_test(
    "func2-1.24",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -0, 1)
    ]], {
        -- <func2-1.24>
        ""
        -- </func2-1.24>
    })

test:do_execsql_test(
    "func2-1.25.0",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -1, 0)
    ]], {
        -- <func2-1.25.0>
        ""
        -- </func2-1.25.0>
    })

test:do_execsql_test(
    "func2-1.25.1",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -1, 1)
    ]], {
        -- <func2-1.25.1>
        "s"
        -- </func2-1.25.1>
    })

test:do_execsql_test(
    "func2-1.25.2",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -1, 2)
    ]], {
        -- <func2-1.25.2>
        "s"
        -- </func2-1.25.2>
    })

test:do_execsql_test(
    "func2-1.26",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -2, 1)
    ]], {
        -- <func2-1.26>
        "u"
        -- </func2-1.26>
    })

test:do_execsql_test(
    "func2-1.27",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -30, 1)
    ]], {
        -- <func2-1.27>
        "r"
        -- </func2-1.27>
    })

test:do_execsql_test(
    "func2-1.28.0",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -34, 0)
    ]], {
        -- <func2-1.28.0>
        ""
        -- </func2-1.28.0>
    })

test:do_execsql_test(
    "func2-1.28.1",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -34, 1)
    ]], {
        -- <func2-1.28.1>
        "S"
        -- </func2-1.28.1>
    })

test:do_execsql_test(
    "func2-1.28.2",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -34, 2)
    ]], {
        -- <func2-1.28.2>
        "Su"
        -- </func2-1.28.2>
    })

test:do_execsql_test(
    "func2-1.29.1",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -35, 1)
    ]], {
        -- <func2-1.29.1>
        ""
        -- </func2-1.29.1>
    })

test:do_execsql_test(
    "func2-1.29.2",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -35, 2)
    ]], {
        -- <func2-1.29.2>
        "S"
        -- </func2-1.29.2>
    })

test:do_execsql_test(
    "func2-1.30.0",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -36, 0)
    ]], {
        -- <func2-1.30.0>
        ""
        -- </func2-1.30.0>
    })

test:do_execsql_test(
    "func2-1.30.1",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -36, 1)
    ]], {
        -- <func2-1.30.1>
        ""
        -- </func2-1.30.1>
    })

test:do_execsql_test(
    "func2-1.30.2",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -36, 2)
    ]], {
        -- <func2-1.30.2>
        ""
        -- </func2-1.30.2>
    })

test:do_execsql_test(
    "func2-1.30.3",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', -36, 3)
    ]], {
        -- <func2-1.30.3>
        "S"
        -- </func2-1.30.3>
    })

-- p1 is 1-indexed, p2 length to return, p2<0 return p2 chars before p1
test:do_execsql_test(
    "func2-1.31.0",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 0, 0)
    ]], {
        -- <func2-1.31.0>
        ""
        -- </func2-1.31.0>
    })

test:do_execsql_test(
    "func2-1.31.1",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 0, -1)
    ]], {
        -- <func2-1.31.1>
        ""
        -- </func2-1.31.1>
    })

test:do_execsql_test(
    "func2-1.31.2",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 0, -2)
    ]], {
        -- <func2-1.31.2>
        ""
        -- </func2-1.31.2>
    })

test:do_execsql_test(
    "func2-1.32.0",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 1, 0)
    ]], {
        -- <func2-1.32.0>
        ""
        -- </func2-1.32.0>
    })

test:do_execsql_test(
    "func2-1.32.1",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 1, -1)
    ]], {
        -- <func2-1.32.1>
        ""
        -- </func2-1.32.1>
    })

test:do_execsql_test(
    "func2-1.33.0",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 2, 0)
    ]], {
        -- <func2-1.33.0>
        ""
        -- </func2-1.33.0>
    })

test:do_execsql_test(
    "func2-1.33.1",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 2, -1)
    ]], {
        -- <func2-1.33.1>
        "S"
        -- </func2-1.33.1>
    })

test:do_execsql_test(
    "func2-1.33.2",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 2, -2)
    ]], {
        -- <func2-1.33.2>
        "S"
        -- </func2-1.33.2>
    })

test:do_execsql_test(
    "func2-1.34.0",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 3, 0)
    ]], {
        -- <func2-1.34.0>
        ""
        -- </func2-1.34.0>
    })

test:do_execsql_test(
    "func2-1.34.1",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 3, -1)
    ]], {
        -- <func2-1.34.1>
        "u"
        -- </func2-1.34.1>
    })

test:do_execsql_test(
    "func2-1.34.2",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 3, -2)
    ]], {
        -- <func2-1.34.2>
        "Su"
        -- </func2-1.34.2>
    })

test:do_execsql_test(
    "func2-1.35.1",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 30, -1)
    ]], {
        -- <func2-1.35.1>
        "o"
        -- </func2-1.35.1>
    })

test:do_execsql_test(
    "func2-1.35.2",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 30, -2)
    ]], {
        -- <func2-1.35.2>
        "do"
        -- </func2-1.35.2>
    })

test:do_execsql_test(
    "func2-1.36",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 34, -1)
    ]], {
        -- <func2-1.36>
        "u"
        -- </func2-1.36>
    })

test:do_execsql_test(
    "func2-1.37",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 35, -1)
    ]], {
        -- <func2-1.37>
        "s"
        -- </func2-1.37>
    })

test:do_execsql_test(
    "func2-1.38.0",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 36, 0)
    ]], {
        -- <func2-1.38.0>
        ""
        -- </func2-1.38.0>
    })

test:do_execsql_test(
    "func2-1.38.1",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 36, -1)
    ]], {
        -- <func2-1.38.1>
        ""
        -- </func2-1.38.1>
    })

test:do_execsql_test(
    "func2-1.38.2",
    [[
        SELECT SUBSTR('Supercalifragilisticexpialidocious', 36, -2)
    ]], {
        -- <func2-1.38.2>
        "s"
        -- </func2-1.38.2>
    })

------------------------------------------------------------------------------
-- Test cases func2-2.*: substr implementation (utf8)
--
-- Only do the following tests if TCL has UTF-8 capabilities
--
if ("ሴ" ~= "u1234")
 then
    test:do_execsql_test(
        "func2-2.1.1",
        [[
            SELECT 'hiሴho'
        ]], {
            -- <func2-2.1.1>
            "hiሴho"
            -- </func2-2.1.1>
        })

    -- substr(x,y), substr(x,y,z)
    test:do_catchsql_test(
        "func2-2.1.2",
        [[
            SELECT SUBSTR()
        ]], {
            -- <func2-2.1.2>
            1, "wrong number of arguments to function SUBSTR()"
            -- </func2-2.1.2>
        })

    test:do_catchsql_test(
        "func2-2.1.3",
        [[
            SELECT SUBSTR('hiሴho')
        ]], {
            -- <func2-2.1.3>
            1, "wrong number of arguments to function SUBSTR()"
            -- </func2-2.1.3>
        })

    test:do_catchsql_test(
        "func2-2.1.4",
        [[
            SELECT SUBSTR('hiሴho', 1,1,1)
        ]], {
            -- <func2-2.1.4>
            1, "wrong number of arguments to function SUBSTR()"
            -- </func2-2.1.4>
        })

    test:do_execsql_test(
        "func2-2.2.0",
        [[
            SELECT SUBSTR('hiሴho', 0, 0)
        ]], {
            -- <func2-2.2.0>
            ""
            -- </func2-2.2.0>
        })

    test:do_execsql_test(
        "func2-2.2.1",
        [[
            SELECT SUBSTR('hiሴho', 0, 1)
        ]], {
            -- <func2-2.2.1>
            ""
            -- </func2-2.2.1>
        })

    test:do_execsql_test(
        "func2-2.2.2",
        [[
            SELECT SUBSTR('hiሴho', 0, 2)
        ]], {
            -- <func2-2.2.2>
            "h"
            -- </func2-2.2.2>
        })

    test:do_execsql_test(
        "func2-2.2.3",
        [[
            SELECT SUBSTR('hiሴho', 0, 3)
        ]], {
            -- <func2-2.2.3>
            "hi"
            -- </func2-2.2.3>
        })

    test:do_execsql_test(
        "func2-2.2.4",
        [[
            SELECT SUBSTR('hiሴho', 0, 4)
        ]], {
            -- <func2-2.2.4>
            "hiሴ"
            -- </func2-2.2.4>
        })

    test:do_execsql_test(
        "func2-2.2.5",
        [[
            SELECT SUBSTR('hiሴho', 0, 5)
        ]], {
            -- <func2-2.2.5>
            "hiሴh"
            -- </func2-2.2.5>
        })

    test:do_execsql_test(
        "func2-2.2.6",
        [[
            SELECT SUBSTR('hiሴho', 0, 6)
        ]], {
            -- <func2-2.2.6>
            "hiሴho"
            -- </func2-2.2.6>
        })

    test:do_execsql_test(
        "func2-2.3.0",
        [[
            SELECT SUBSTR('hiሴho', 1, 0)
        ]], {
            -- <func2-2.3.0>
            ""
            -- </func2-2.3.0>
        })

    test:do_execsql_test(
        "func2-2.3.1",
        [[
            SELECT SUBSTR('hiሴho', 1, 1)
        ]], {
            -- <func2-2.3.1>
            "h"
            -- </func2-2.3.1>
        })

    test:do_execsql_test(
        "func2-2.3.2",
        [[
            SELECT SUBSTR('hiሴho', 1, 2)
        ]], {
            -- <func2-2.3.2>
            "hi"
            -- </func2-2.3.2>
        })

    test:do_execsql_test(
        "func2-2.3.3",
        [[
            SELECT SUBSTR('hiሴho', 1, 3)
        ]], {
            -- <func2-2.3.3>
            "hiሴ"
            -- </func2-2.3.3>
        })

    test:do_execsql_test(
        "func2-2.3.4",
        [[
            SELECT SUBSTR('hiሴho', 1, 4)
        ]], {
            -- <func2-2.3.4>
            "hiሴh"
            -- </func2-2.3.4>
        })

    test:do_execsql_test(
        "func2-2.3.5",
        [[
            SELECT SUBSTR('hiሴho', 1, 5)
        ]], {
            -- <func2-2.3.5>
            "hiሴho"
            -- </func2-2.3.5>
        })

    test:do_execsql_test(
        "func2-2.3.6",
        [[
            SELECT SUBSTR('hiሴho', 1, 6)
        ]], {
            -- <func2-2.3.6>
            "hiሴho"
            -- </func2-2.3.6>
        })

    test:do_execsql_test(
        "func2-2.4.0",
        [[
            SELECT SUBSTR('hiሴho', 3, 0)
        ]], {
            -- <func2-2.4.0>
            ""
            -- </func2-2.4.0>
        })

    test:do_execsql_test(
        "func2-2.4.1",
        [[
            SELECT SUBSTR('hiሴho', 3, 1)
        ]], {
            -- <func2-2.4.1>
            "ሴ"
            -- </func2-2.4.1>
        })

    test:do_execsql_test(
        "func2-2.4.2",
        [[
            SELECT SUBSTR('hiሴho', 3, 2)
        ]], {
            -- <func2-2.4.2>
            "ሴh"
            -- </func2-2.4.2>
        })

    test:do_execsql_test(
        "func2-2.5.0",
        [[
            SELECT SUBSTR('ሴ', 0, 0)
        ]], {
            -- <func2-2.5.0>
            ""
            -- </func2-2.5.0>
        })

    test:do_execsql_test(
        "func2-2.5.1",
        [[
            SELECT SUBSTR('ሴ', 0, 1)
        ]], {
            -- <func2-2.5.1>
            ""
            -- </func2-2.5.1>
        })

    test:do_execsql_test(
        "func2-2.5.2",
        [[
            SELECT SUBSTR('ሴ', 0, 2)
        ]], {
            -- <func2-2.5.2>
            "ሴ"
            -- </func2-2.5.2>
        })

    test:do_execsql_test(
        "func2-2.5.3",
        [[
            SELECT SUBSTR('ሴ', 0, 3)
        ]], {
            -- <func2-2.5.3>
            "ሴ"
            -- </func2-2.5.3>
        })

    test:do_execsql_test(
        "func2-2.6.0",
        [[
            SELECT SUBSTR('ሴ', 1, 0)
        ]], {
            -- <func2-2.6.0>
            ""
            -- </func2-2.6.0>
        })

    test:do_execsql_test(
        "func2-2.6.1",
        [[
            SELECT SUBSTR('ሴ', 1, 1)
        ]], {
            -- <func2-2.6.1>
            "ሴ"
            -- </func2-2.6.1>
        })

    test:do_execsql_test(
        "func2-2.6.2",
        [[
            SELECT SUBSTR('ሴ', 1, 2)
        ]], {
            -- <func2-2.6.2>
            "ሴ"
            -- </func2-2.6.2>
        })

    test:do_execsql_test(
        "func2-2.6.3",
        [[
            SELECT SUBSTR('ሴ', 1, 3)
        ]], {
            -- <func2-2.6.3>
            "ሴ"
            -- </func2-2.6.3>
        })

    test:do_execsql_test(
        "func2-2.7.0",
        [[
            SELECT SUBSTR('ሴ', 2, 0)
        ]], {
            -- <func2-2.7.0>
            ""
            -- </func2-2.7.0>
        })

    test:do_execsql_test(
        "func2-2.7.1",
        [[
            SELECT SUBSTR('ሴ', 2, 1)
        ]], {
            -- <func2-2.7.1>
            ""
            -- </func2-2.7.1>
        })

    test:do_execsql_test(
        "func2-2.7.2",
        [[
            SELECT SUBSTR('ሴ', 2, 2)
        ]], {
            -- <func2-2.7.2>
            ""
            -- </func2-2.7.2>
        })

    test:do_execsql_test(
        "func2-2.8.0",
        [[
            SELECT SUBSTR('ሴ', -1, 0)
        ]], {
            -- <func2-2.8.0>
            ""
            -- </func2-2.8.0>
        })

    test:do_execsql_test(
        "func2-2.8.1",
        [[
            SELECT SUBSTR('ሴ', -1, 1)
        ]], {
            -- <func2-2.8.1>
            "ሴ"
            -- </func2-2.8.1>
        })

    test:do_execsql_test(
        "func2-2.8.2",
        [[
            SELECT SUBSTR('ሴ', -1, 2)
        ]], {
            -- <func2-2.8.2>
            "ሴ"
            -- </func2-2.8.2>
        })

    test:do_execsql_test(
        "func2-2.8.3",
        [[
            SELECT SUBSTR('ሴ', -1, 3)
        ]], {
            -- <func2-2.8.3>
            "ሴ"
            -- </func2-2.8.3>
        })

end
-- End \u1234!=u1234
------------------------------------------------------------------------------
-- Test cases func2-3.*: substr implementation (blob)
--


test:do_test(
    "func2-3.1.1",
    function()
        blob = test:execsql "SELECT x'1234'"
        return bin_to_hex(test.lindex(blob, 0))
    end, "1234")

-- substr(x,y), substr(x,y,z)
test:do_catchsql_test(
    "func2-3.1.2",
    [[
        SELECT SUBSTR()
    ]], {
        -- <func2-3.1.2>
        1, "wrong number of arguments to function SUBSTR()"
        -- </func2-3.1.2>
    })

test:do_catchsql_test(
    "func2-3.1.3",
    [[
        SELECT SUBSTR(x'1234')
    ]], {
        -- <func2-3.1.3>
        1, "wrong number of arguments to function SUBSTR()"
        -- </func2-3.1.3>
    })

test:do_catchsql_test(
    "func2-3.1.4",
    [[
        SELECT SUBSTR(x'1234', 1,1,1)
    ]], {
        -- <func2-3.1.4>
        1, "wrong number of arguments to function SUBSTR()"
        -- </func2-3.1.4>
    })

test:do_test(
    "func2-3.2.0",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', 0, 0)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "")

test:do_test(
    "func2-3.2.1",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', 0, 1)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "")

test:do_test(
    "func2-3.2.2",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', 0, 2)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "12")

test:do_test(
    "func2-3.2.3",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', 0, 3)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "1234")

test:do_test(
    "func2-3.3.0",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', 1, 0)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "")

test:do_test(
    "func2-3.3.1",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', 1, 1)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "12")

test:do_test(
    "func2-3.3.2",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', 1, 2)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "1234")

test:do_test(
    "func2-3.3.3",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', 1, 3)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "1234")

test:do_test(
    "func2-3.4.0",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', -1, 0)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "")

test:do_test(
    "func2-3.4.1",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', -1, 1)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "34")

test:do_test(
    "func2-3.4.2",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', -1, 2)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "34")

test:do_test(
    "func2-3.4.3",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', -1, 3)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "34")

test:do_test(
    "func2-3.5.0",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', -2, 0)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "")

test:do_test(
    "func2-3.5.1",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', -2, 1)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "12")

test:do_test(
    "func2-3.5.2",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', -2, 2)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "1234")

test:do_test(
    "func2-3.5.3",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', -2, 3)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "1234")

test:do_test(
    "func2-3.6.0",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', -1, 0)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "")

test:do_test(
    "func2-3.6.1",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', -1, -1)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "12")

test:do_test(
    "func2-3.6.2",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', -1, -2)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "12")

test:do_test(
    "func2-3.6.3",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', -1, -3)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "12")

test:do_test(
    "func2-3.7.0",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', -2, 0)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "")

test:do_test(
    "func2-3.7.1",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', -2, -1)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "")

test:do_test(
    "func2-3.7.2",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', -2, -2)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "")

test:do_test(
    "func2-3.8.0",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', 1, 0)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "")

test:do_test(
    "func2-3.8.1",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', 1, -1)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "")

test:do_test(
    "func2-3.8.2",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', 1, -2)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "")

test:do_test(
    "func2-3.9.0",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', 2, 0)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "")

test:do_test(
    "func2-3.9.1",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', 2, -1)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "12")

test:do_test(
    "func2-3.9.2",
    function()
        blob = test:execsql "SELECT SUBSTR(x'1234', 2, -2)"
        return bin_to_hex(test.lindex(blob, 0))
    end, "12")



test:finish_test()
