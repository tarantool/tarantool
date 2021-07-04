#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(14694)

--!./tcltestrunner.lua
-- 2001 September 15
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
-- focus of this file is testing built-in functions.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- Create a table to work with.
--
test:do_test(
    "func-0.0",
    function()
        test:execsql("CREATE TABLE tbl1(id integer primary key, t1 text)")
        for i, word in ipairs({"this", "program", "is", "free", "software"}) do
            test:execsql(string.format("INSERT INTO tbl1(id, t1) VALUES(%s, '%s')", i, word))
        end
        return test:execsql("SELECT t1 FROM tbl1 ORDER BY t1")
    end, {
        -- <func-0.0>
        "free", "is", "program", "software", "this"
        -- </func-0.0>
    })

test:do_execsql_test(
    "func-0.1",
    [[
        CREATE TABLE t2(id integer primary key, a INT);
        INSERT INTO t2(id,a) VALUES(1, 1);
        INSERT INTO t2(id,a) VALUES(2, NULL);
        INSERT INTO t2(id,a) VALUES(3, 345);
        INSERT INTO t2(id,a) VALUES(4, NULL);
        INSERT INTO t2(id,a) VALUES(5, 67890);
        SELECT a FROM t2;
    ]], {
        -- <func-0.1>
        1, "", 345, "", 67890
        -- </func-0.1>
    })

-- Check out the length() function
--
test:do_execsql_test(
    "func-1.0",
    [[
        SELECT length(t1) FROM tbl1 ORDER BY t1
    ]], {
        -- <func-1.0>
        4, 2, 7, 8, 4
        -- </func-1.0>
    })

test:do_catchsql_test(
    "func-1.1",
    [[
        SELECT length(*) FROM tbl1 ORDER BY t1
    ]], {
        -- <func-1.1>
        1, "Wrong number of arguments is passed to LENGTH(): expected 1, got 0"
        -- </func-1.1>
    })

test:do_catchsql_test(
    "func-1.2",
    [[
        SELECT length(t1,5) FROM tbl1 ORDER BY t1
    ]], {
        -- <func-1.2>
        1, "Wrong number of arguments is passed to LENGTH(): expected 1, got 2"
        -- </func-1.2>
    })

test:do_execsql_test(
    "func-1.3",
    [[SELECT length(t1), count(t1) FROM tbl1 GROUP BY length(t1)
           ORDER BY length(t1)]], {
        -- <func-1.3>
        2, 1, 4, 2, 7, 1, 8, 1
        -- </func-1.3>
    })

test:do_execsql_test(
    "func-1.4",
    [[
        SELECT coalesce(length(a),-1) FROM t2
    ]], {
        -- <func-1.4>
        1, -1, 3, -1, 5
        -- </func-1.4>
    })

-- Check out the substr() function
--
test:do_execsql_test(
    "func-2.0",
    [[
        SELECT substr(t1,1,2) FROM tbl1 ORDER BY t1
    ]], {
        -- <func-2.0>
        "fr", "is", "pr", "so", "th"
        -- </func-2.0>
    })

test:do_execsql_test(
    "func-2.1",
    [[
        SELECT substr(t1,2,1) FROM tbl1 ORDER BY t1
    ]], {
        -- <func-2.1>
        "r", "s", "r", "o", "h"
        -- </func-2.1>
    })

test:do_execsql_test(
    "func-2.2",
    [[
        SELECT substr(t1,3,3) FROM tbl1 ORDER BY t1
    ]], {
        -- <func-2.2>
        "ee", "", "ogr", "ftw", "is"
        -- </func-2.2>
    })

test:do_execsql_test(
    "func-2.3",
    [[
        SELECT substr(t1,-1,1) FROM tbl1 ORDER BY t1
    ]], {
        -- <func-2.3>
        "e", "s", "m", "e", "s"
        -- </func-2.3>
    })

test:do_execsql_test(
    "func-2.4",
    [[
        SELECT substr(t1,-1,2) FROM tbl1 ORDER BY t1
    ]], {
        -- <func-2.4>
        "e", "s", "m", "e", "s"
        -- </func-2.4>
    })

test:do_execsql_test(
    "func-2.5",
    [[
        SELECT substr(t1,-2,1) FROM tbl1 ORDER BY t1
    ]], {
        -- <func-2.5>
        "e", "i", "a", "r", "i"
        -- </func-2.5>
    })

test:do_execsql_test(
    "func-2.6",
    [[
        SELECT substr(t1,-2,2) FROM tbl1 ORDER BY t1
    ]], {
        -- <func-2.6>
        "ee", "is", "am", "re", "is"
        -- </func-2.6>
    })

test:do_execsql_test(
    "func-2.7",
    [[
        SELECT substr(t1,-4,2) FROM tbl1 ORDER BY t1
    ]], {
        -- <func-2.7>
        "fr", "", "gr", "wa", "th"
        -- </func-2.7>
    })

test:do_execsql_test(
    "func-2.8",
    [[
        SELECT t1 FROM tbl1 ORDER BY substr(t1,2,20)
    ]], {
        -- <func-2.8>
        "this", "software", "free", "program", "is"
        -- </func-2.8>
    })

test:do_execsql_test(
    "func-2.9",
    [[
        SELECT substr(a,1,1) FROM t2
    ]], {
        -- <func-2.9>
        "1", "", "3", "", "6"
        -- </func-2.9>
    })

test:do_execsql_test(
    "func-2.10",
    [[
        SELECT substr(a,2,2) FROM t2
    ]], {
        -- <func-2.10>
        "", "", "45", "", "78"
        -- </func-2.10>
    })

-- Only do the following tests if TCL has UTF-8 capabilities
--
if ("ሴ" ~= "u1234") then
    -- Put some UTF-8 characters in the database
    --
    test:do_test(
        "func-3.0",
        function()
            test:execsql("DELETE FROM tbl1")
            for i, word in ipairs({"contains", "UTF-8", "characters", "hiሴho"}) do
                test:execsql(string.format("INSERT INTO tbl1(id, t1) VALUES(%s, '%s')", i, word))
            end
            return test:execsql("SELECT t1 FROM tbl1 ORDER BY t1")
        end, {
            -- <func-3.0>
            "UTF-8", "characters", "contains", "hiሴho"
            -- </func-3.0>
        })

    test:do_execsql_test(
        "func-3.1",
        [[
            SELECT length(t1) FROM tbl1 ORDER BY t1
        ]], {
            -- <func-3.1>
            5, 10, 8, 5
            -- </func-3.1>
        })

    test:do_execsql_test(
        "func-3.2",
        [[
            SELECT substr(t1,1,2) FROM tbl1 ORDER BY t1
        ]], {
            -- <func-3.2>
            "UT", "ch", "co", "hi"
            -- </func-3.2>
        })

    test:do_execsql_test(
        "func-3.3",
        [[
            SELECT substr(t1,1,3) FROM tbl1 ORDER BY t1
        ]], {
            -- <func-3.3>
            "UTF", "cha", "con", "hiሴ"
            -- </func-3.3>
        })

    test:do_execsql_test(
        "func-3.4",
        [[
            SELECT substr(t1,2,2) FROM tbl1 ORDER BY t1
        ]], {
            -- <func-3.4>
            "TF", "ha", "on", "iሴ"
            -- </func-3.4>
        })

    test:do_execsql_test(
        "func-3.5",
        [[
            SELECT substr(t1,2,3) FROM tbl1 ORDER BY t1
        ]], {
            -- <func-3.5>
            "TF-", "har", "ont", "iሴh"
            -- </func-3.5>
        })

    test:do_execsql_test(
        "func-3.6",
        [[
            SELECT substr(t1,3,2) FROM tbl1 ORDER BY t1
        ]], {
            -- <func-3.6>
            "F-", "ar", "nt", "ሴh"
            -- </func-3.6>
        })

    test:do_execsql_test(
        "func-3.7",
        [[
            SELECT substr(t1,4,2) FROM tbl1 ORDER BY t1
        ]], {
            -- <func-3.7>
            "-8", "ra", "ta", "ho"
            -- </func-3.7>
        })

    test:do_execsql_test(
        "func-3.8",
        [[
            SELECT substr(t1,-1,1) FROM tbl1 ORDER BY t1
        ]], {
            -- <func-3.8>
            "8", "s", "s", "o"
            -- </func-3.8>
        })

    test:do_execsql_test(
        "func-3.9",
        [[
            SELECT substr(t1,-3,2) FROM tbl1 ORDER BY t1
        ]], {
            -- <func-3.9>
            "F-", "er", "in", "ሴh"
            -- </func-3.9>
        })

    test:do_execsql_test(
        "func-3.10",
        [[
            SELECT substr(t1,-4,3) FROM tbl1 ORDER BY t1
        ]], {
            -- <func-3.10>
            "TF-", "ter", "ain", "iሴh"
            -- </func-3.10>
        })

    test:do_test(
        "func-3.99",
        function()
            test:execsql("DELETE FROM tbl1")
            for i, word in ipairs({"this", "program", "is", "free", "software"}) do
                test:execsql(string.format("INSERT INTO tbl1(id, t1) VALUES(%s, '%s')", i, word))
            end
            return test:execsql("SELECT t1 FROM tbl1")
        end, {
            -- <func-3.99>
            "this", "program", "is", "free", "software"
            -- </func-3.99>
        })

end
-- End \u1234!=u1234
-- Test the abs() and round() functions.
--


test:do_test(
    "func-4.1",
    function()
        test:execsql([[
            CREATE TABLE t1(id integer primary key, a INT,b NUMBER,c NUMBER);
            INSERT INTO t1(id, a,b,c) VALUES(1, 1,2,3);
            INSERT INTO t1(id, a,b,c) VALUES(2, 2,1.2345678901234,-12345.67890);
            INSERT INTO t1(id, a,b,c) VALUES(3, 3,-2,-5);
        ]])
        return test:catchsql("SELECT abs(a,b) FROM t1")
    end, {
        -- <func-4.1>
        1, "Wrong number of arguments is passed to ABS(): expected 1, got 2"
        -- </func-4.1>
    })



test:do_catchsql_test(
    "func-4.2",
    [[
        SELECT abs() FROM t1
    ]], {
        -- <func-4.2>
        1, "Wrong number of arguments is passed to ABS(): expected 1, got 0"
        -- </func-4.2>
    })

test:do_catchsql_test(
    "func-4.3",
    [[
        SELECT abs(b) FROM t1 ORDER BY a
    ]], {
        -- <func-4.3>
        0, {2, 1.2345678901234, 2}
        -- </func-4.3>
    })

test:do_catchsql_test(
    "func-4.4",
    [[
        SELECT abs(c) FROM t1 ORDER BY a
    ]], {
        -- <func-4.4>
        0, {3, 12345.6789, 5}
        -- </func-4.4>
    })


test:do_execsql_test(
    "func-4.4.1",
    [[
        SELECT abs(a) FROM t2
    ]], {
        -- <func-4.4.1>
        1, "", 345, "", 67890
        -- </func-4.4.1>
    })

test:do_execsql_test(
    "func-4.4.2",
    [[
        SELECT abs(t1) FROM tbl1
    ]], {
        -- <func-4.4.2>
        0.0, 0.0, 0.0, 0.0, 0.0
        -- </func-4.4.2>
    })

test:do_catchsql_test(
    "func-4.5",
    [[
        SELECT round(a,b,c) FROM t1
    ]], {
        -- <func-4.5>
        1, "Wrong number of arguments is passed to ROUND(): expected 1 or 2, got 3"
        -- </func-4.5>
    })

test:do_catchsql_test(
    "func-4.6",
    [[
        SELECT round(b,2) FROM t1 ORDER BY b
    ]], {
        -- <func-4.6>
        0, {-2.0, 1.23, 2.0}
        -- </func-4.6>
    })

test:do_catchsql_test(
    "func-4.7",
    [[
        SELECT round(b,0) FROM t1 ORDER BY a
    ]], {
        -- <func-4.7>
        0, {2.0, 1.0, -2.0}
        -- </func-4.7>
    })

test:do_catchsql_test(
    "func-4.8",
    [[
        SELECT round(c) FROM t1 ORDER BY a
    ]], {
        -- <func-4.8>
        0, {3.0, -12346.0, -5.0}
        -- </func-4.8>
    })

test:do_catchsql_test(
    "func-4.9",
    [[
        SELECT round(c,a) FROM t1 ORDER BY a
    ]], {
        -- <func-4.9>
        0, {3.0, -12345.68, -5.0}
        -- </func-4.9>
    })

test:do_catchsql_test(
    "func-4.10",
    [[
        SELECT 'x' || CAST(round(c,a) AS TEXT) || 'y' FROM t1 ORDER BY a
    ]], {
        -- <func-4.10>
        0, {"x3.0y", "x-12345.68y", "x-5.0y"}
        -- </func-4.10>
    })

test:do_catchsql_test(
    "func-4.11",
    [[
        SELECT round() FROM t1 ORDER BY a
    ]], {
        -- <func-4.11>
        1, "Wrong number of arguments is passed to ROUND(): expected 1 or 2, got 0"
        -- </func-4.11>
    })

test:do_execsql_test(
    "func-4.12",
    [[
        SELECT coalesce(round(a,2),'nil') FROM t2
    ]], {
        -- <func-4.12>
        1.0, "nil", 345.0, "nil", 67890.0
        -- </func-4.12>
    })

test:do_execsql_test(
    "func-4.13",
    [[
        SELECT round(t1,2) FROM tbl1
    ]], {
        -- <func-4.13>
        0.0, 0.0, 0.0, 0.0, 0.0
        -- </func-4.13>
    })

test:do_execsql_test(
    "func-4.14",
    [[
        SELECT typeof(round(5.1,1));
    ]], {
        -- <func-4.14>
        "double"
        -- </func-4.14>
    })

test:do_execsql_test(
    "func-4.15",
    [[
        SELECT typeof(round(5.1));
    ]], {
        -- <func-4.15>
        "double"
        -- </func-4.15>
    })

test:do_catchsql_test(
    "func-4.16",
    [[
        SELECT round(b,2.0) FROM t1 ORDER BY b
    ]], {
        -- <func-4.16>
        0, {-2.0, 1.23, 2.0}
        -- </func-4.16>
    })

-- Verify some values reported on the mailing list.
-- Some of these fail on MSVC builds with 64-bit
-- long doubles, but not on GCC builds with 80-bit
-- long doubles.
for i = 1, 1000-1, 1 do
    local x1 = (40222.5 + i)
    local x2 = (40223 + i)
    test:do_execsql_test(
        "func-4.17."..i,
        "SELECT round("..x1..");", {
            x2
        })

end
for i = 1, 1000-1, 1 do
    local x1 = (40222.05 + i)
    local x2 = (40222.1 + i)
    test:do_execsql_test(
        "func-4.18."..i,
        "SELECT round("..x1..",1);", {
            x2
        })

end
test:do_execsql_test(
    "func-4.20",
    [[
        SELECT round(40223.4999999999);
    ]], {
        -- <func-4.20>
        40223.0
        -- </func-4.20>
    })

test:do_execsql_test(
    "func-4.21",
    [[
        SELECT round(40224.4999999999);
    ]], {
        -- <func-4.21>
        40224.0
        -- </func-4.21>
    })

test:do_execsql_test(
    "func-4.22",
    [[
        SELECT round(40225.4999999999);
    ]], {
        -- <func-4.22>
        40225.0
        -- </func-4.22>
    })

for i = 1, 9, 1 do
    test:do_execsql_test(
        "func-4.23."..i,
        string.format("SELECT round(40223.4999999999,%s);", i), {
            40223.5
        })

    test:do_execsql_test(
        "func-4.24."..i,
        string.format("SELECT round(40224.4999999999,%s);", i), {
            40224.5
        })

    test:do_execsql_test(
        "func-4.25."..i,
        string.format("SELECT round(40225.4999999999,%s);", i), {
            40225.5
        })

end
for i = 10, 31, 1 do
    test:do_execsql_test(
        "func-4.26."..i,
        string.format("SELECT round(40223.4999999999,%s);", i), {
            40223.4999999999
        })

    test:do_execsql_test(
        "func-4.27."..i,
        string.format("SELECT round(40224.4999999999,%s);", i), {
            40224.4999999999
        })

    test:do_execsql_test(
        "func-4.28."..i,
        string.format("SELECT round(40225.4999999999,%s);", i), {
            40225.4999999999
        })

end
test:do_execsql_test(
    "func-4.29",
    [[
        SELECT round(1234567890.5);
    ]], {
        -- <func-4.29>
        1234567891.0
        -- </func-4.29>
    })

test:do_execsql_test(
    "func-4.30",
    [[
        SELECT round(12345678901.5);
    ]], {
        -- <func-4.30>
        12345678902.0
        -- </func-4.30>
    })

test:do_execsql_test(
    "func-4.31",
    [[
        SELECT round(123456789012.5);
    ]], {
        -- <func-4.31>
        123456789013.0
        -- </func-4.31>
    })

test:do_execsql_test(
    "func-4.32",
    [[
        SELECT round(1234567890123.5);
    ]], {
        -- <func-4.32>
        1234567890124.0
        -- </func-4.32>
    })

test:do_execsql_test(
    "func-4.33",
    [[
        SELECT round(12345678901234.5);
    ]], {
        -- <func-4.33>
        12345678901235.0
        -- </func-4.33>
    })

test:do_execsql_test(
    "func-4.34",
    [[
        SELECT round(1234567890123.35,1);
    ]], {
        -- <func-4.34>
        1234567890123.4
        -- </func-4.34>
    })

test:do_execsql_test(
    "func-4.35",
    [[
        SELECT round(1234567890123.445,2);
    ]], {
        -- <func-4.35>
        1234567890123.45
        -- </func-4.35>
    })

test:do_execsql_test(
    "func-4.36",
    [[
        SELECT round(99999999999994.5);
    ]], {
        -- <func-4.36>
        99999999999995.0
        -- </func-4.36>
    })

test:do_execsql_test(
    "func-4.37",
    [[
        SELECT round(9999999999999.55,1);
    ]], {
        -- <func-4.37>
        9999999999999.6
        -- </func-4.37>
    })

test:do_execsql_test(
    "func-4.38",
    [[
        SELECT round(9999999999999.556,2);
    ]], {
        -- <func-4.38>
        9999999999999.56
        -- </func-4.38>
    })



-- Test the upper() and lower() functions
--
test:do_execsql_test(
    "func-5.1",
    [[
        SELECT upper(t1) FROM tbl1
    ]], {
        -- <func-5.1>
        "THIS", "PROGRAM", "IS", "FREE", "SOFTWARE"
        -- </func-5.1>
    })

test:do_execsql_test(
    "func-5.2",
    [[
        SELECT lower(upper(t1)) FROM tbl1
    ]], {
        -- <func-5.2>
        "this", "program", "is", "free", "software"
        -- </func-5.2>
    })

test:do_execsql_test(
    "func-5.3",
    [[
        SELECT upper(a), lower(a) FROM t2
    ]], {
        -- <func-5.3>
        "1","1","","","345","345","","","67890","67890"
        -- </func-5.3>
    })



test:do_catchsql_test(
    "func-5.5",
    [[
        SELECT upper(*) FROM t2
    ]], {
        -- <func-5.5>
        1, "Wrong number of arguments is passed to UPPER(): expected 1, got 0"
        -- </func-5.5>
    })

-- Test the coalesce() and nullif() functions
--
test:do_execsql_test(
    "func-6.1",
    [[
        SELECT coalesce(a,'xyz') FROM t2
    ]], {
        -- <func-6.1>
        1, "xyz", 345, "xyz", 67890
        -- </func-6.1>
    })

test:do_execsql_test(
    "func-6.2",
    [[
        SELECT coalesce(upper(a),'nil') FROM t2
    ]], {
        -- <func-6.2>
        "1","nil","345","nil","67890"
        -- </func-6.2>
    })

test:do_execsql_test(
    "func-6.3",
    [[
        SELECT coalesce(nullif(1,1),'nil')
    ]], {
        -- <func-6.3>
        "nil"
        -- </func-6.3>
    })

test:do_execsql_test(
    "func-6.4",
    [[
        SELECT coalesce(nullif(1,2),'nil')
    ]], {
        -- <func-6.4>
        1
        -- </func-6.4>
    })

test:do_execsql_test(
    "func-6.5",
    [[
        SELECT coalesce(nullif(1,NULL),'nil')
    ]], {
        -- <func-6.5>
        1
        -- </func-6.5>
    })

-- # Test the last_insert_rowid() function
-- #
-- do_test func-7.1 {
--   execsql {SELECT last_insert_rowid()}
-- } [db last_insert_rowid]
-- Tests for aggregate functions and how they handle NULLs.
--
test:do_test(
    "func-8.1",
    function()
        test:execsql("EXPLAIN SELECT sum(a) FROM t2;")


        return test:execsql([[
            SELECT sum(a), count(a), round(avg(a),2), min(a), max(a), count(*) FROM t2;
        ]])
    end, {
        -- <func-8.1>
        68236, 3, 22745.33, 1, 67890, 5
        -- </func-8.1>
    })





test:do_execsql_test(
    "func-8.2",
    [[
        SELECT max('z+'|| CAST(a AS TEXT) ||'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOP') FROM t2;
    ]], {
        -- <func-8.2>
        "z+67890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOP"
        -- </func-8.2>
    })

-- ifcapable tempdb {
--   do_test func-8.3 {
--     execsql {
--       CREATE TEMP TABLE t3 AS SELECT a FROM t2 ORDER BY a DESC;
--       SELECT min('z+'||a||'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOP') FROM t3;
--     }
--   } {z+1abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOP}
-- } else {
--   do_test func-8.3 {
--     execsql {
--       CREATE TABLE t3 AS SELECT a FROM t2 ORDER BY a DESC;
--       SELECT min('z+'||a||'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOP') FROM t3;
--     }
--   } {z+1abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOP}
-- }
-- do_test func-8.4 {
--   execsql {
--     SELECT max('z+'||a||'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOP') FROM t3;
--   }
-- } {z+67890abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOP}
test:do_execsql_test(
    "func-8.5",
    [[
        SELECT sum(x) FROM (SELECT '9223372036' || '854775807' AS x
                            UNION ALL SELECT -9223372036854775807)
    ]], {
        -- <func-8.5>
        0
        -- </func-8.5>
    })

test:do_execsql_test(
    "func-8.6",
    [[
        SELECT typeof(sum(x)) FROM (SELECT '9223372036' || '854775807' AS x
                            UNION ALL SELECT -9223372036854775807)
    ]], {
        -- <func-8.6>
        "integer"
        -- </func-8.6>
    })

test:do_execsql_test(
    "func-8.7",
    [[
        SELECT typeof(sum(x)) FROM (SELECT '9223372036' || '854775808' AS x
                            UNION ALL SELECT -9223372036854775807)
    ]], {
        -- <func-8.7>
        "integer"
        -- </func-8.7>
    })

test:do_execsql_test(
    "func-8.8",
    [[
        SELECT sum(x)>0.0 FROM (SELECT '9223372036' || '854775808' AS x
                            UNION ALL SELECT -9223372036850000000)
    ]], {
        -- <func-8.8>
        true
        -- </func-8.8>
    })







-- How do you test the random() function in a meaningful, deterministic way?
--
test:do_execsql_test(
    "func-9.1",
    [[
        SELECT random() is not null;
    ]], {
        -- <func-9.1>
        true
        -- </func-9.1>
    })

test:do_execsql_test(
    "func-9.2",
    [[
        SELECT typeof(random());
    ]], {
        -- <func-9.2>
        "integer"
        -- </func-9.2>
    })

test:do_execsql_test(
    "func-9.3",
    [[
        SELECT randomblob(32) is not null;
    ]], {
        -- <func-9.3>
        true
        -- </func-9.3>
    })

test:do_execsql_test(
    "func-9.4",
    [[
        SELECT typeof(randomblob(32));
    ]], {
        -- <func-9.4>
        "varbinary"
        -- </func-9.4>
    })

test:do_execsql_test(
    "func-9.5",
    [[
        SELECT length(randomblob(32)), length(randomblob(-5)),
               length(randomblob(2000))
    ]], {
        -- <func-9.5>
        32, "", 2000
        -- </func-9.5>
    })

-- The "hex()" function was added in order to be able to render blobs
-- generated by randomblob().  So this seems like a good place to test
-- hex().
--
test:do_execsql_test(
    "func-9.10",
    [[
        SELECT hex(x'00112233445566778899aAbBcCdDeEfF')
    ]], {
        -- <func-9.10>
        "00112233445566778899AABBCCDDEEFF"
        -- </func-9.10>
    })



--encoding = db("one", "PRAGMA encoding")

test:do_execsql_test(
    "func-9.11-utf8",
    [[
        SELECT hex(replace('abcdefg','ef','12'))
    ]], {
    -- <func-9.11-utf8>
    "61626364313267"
    -- </func-9.11-utf8>
})

test:do_execsql_test(
    "func-9.12-utf8",
    [[
        SELECT hex(replace('abcdefg','','12'))
    ]], {
    -- <func-9.12-utf8>
    "61626364656667"
    -- </func-9.12-utf8>
})

test:do_execsql_test(
    "func-9.13-utf8",
    [[
        SELECT hex(replace('aabcdefg','a','aaa'))
    ]], {
    -- <func-9.13-utf8>
    "616161616161626364656667"
    -- </func-9.13-utf8>
})

-- Use the "sql_register_test_function" TCL command which is part of
-- the text fixture in order to verify correct operation of some of
-- the user-defined SQL function APIs that are not used by the built-in
-- functions.
--
-- MUST_WORK_TEST testfunc not implemented
if 0 > 0 then
-- Legacy from the original code. Must be replaced with analogue
-- functions from box.
local X = nil
X(525, "X!cmd", [=[["sql_register_test_function",["::DB"],"testfunc"]]=])
test:do_catchsql_test(
    "func-10.1",
    [[
        SELECT testfunc(NULL,NULL);
    ]], {
        -- <func-10.1>
        1, "first argument should be one of: int int64 string double null value"
        -- </func-10.1>
    })

test:do_execsql_test(
    "func-10.2",
    [[
        SELECT testfunc(
         'string', 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ',
         'int', 1234
        );
    ]], {
        -- <func-10.2>
        1234
        -- </func-10.2>
    })

test:do_execsql_test(
    "func-10.3",
    [[
        SELECT testfunc(
         'string', 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ',
         'string', NULL
        );
    ]], {
        -- <func-10.3>
        ""
        -- </func-10.3>
    })

test:do_execsql_test(
    "func-10.4",
    [[
        SELECT testfunc(
         'string', 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ',
         'double', 1.234
        );
    ]], {
        -- <func-10.4>
        1.234
        -- </func-10.4>
    })

test:do_execsql_test(
    "func-10.5",
    [[
        SELECT testfunc(
         'string', 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ',
         'int', 1234,
         'string', 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ',
         'string', NULL,
         'string', 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ',
         'double', 1.234,
         'string', 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ',
         'int', 1234,
         'string', 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ',
         'string', NULL,
         'string', 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ',
         'double', 1.234
        );
    ]], {
        -- <func-10.5>
        1.234
        -- </func-10.5>
    })
end


-- # Test the built-in sql_version(*) SQL function.
-- #
-- do_test func-11.1 {
--   execsql {
--     SELECT sql_version(*);
--   }
-- } [sql -version]
-- Test that destructors passed to sql by calls to sql_result_text()
-- etc. are called. These tests use two special user-defined functions
-- (implemented in func.c) only available in test builds.
--
-- Function test_destructor() takes one argument and returns a copy of the
-- text form of that argument. A destructor is associated with the return
-- value. Function test_destructor_count() returns the number of outstanding
-- destructor calls for values returned by test_destructor().
--
-- MUST_WORK_TEST test_destructor_count not implemented
if 0 > 0 then
local X = nil
-- Legacy from the original code. Must be replaced with analogue
-- functions from box.
local sql_prepare = nil
local sql_bind_text = nil
local sql_bind_blob = nil
local sql_column_text = nil
local sql_step = nil
local sql_finalize = nil
local db = nil

if X(595, "X!cmd", "[\"expr\",\"[db eval {PRAGMA encoding}]==\\\"UTF-8\\\"\"]")
 then
    test:do_execsql_test(
        "func-12.1-utf8",
        [[
            SELECT test_destructor('hello world'), test_destructor_count();
        ]], {
            -- <func-12.1-utf8>
            "hello world", 1
            -- </func-12.1-utf8>
        })

else
    test:do_execsql_test(
        "func-12.1-utf16",
        [[
            SELECT test_destructor16('hello world'), test_destructor_count();
        ]], {
            -- <func-12.1-utf16>
            "hello world", 1
            -- </func-12.1-utf16>
        })



end
test:do_execsql_test(
    "func-12.2",
    [[
        SELECT test_destructor_count();
    ]], {
        -- <func-12.2>
        0
        -- </func-12.2>
    })

test:do_execsql_test(
    "func-12.3",
    [[
        SELECT test_destructor('hello')||' world'
    ]], {
        -- <func-12.3>
        "hello world"
        -- </func-12.3>
    })

test:do_execsql_test(
    "func-12.4",
    [[
        SELECT test_destructor_count();
    ]], {
        -- <func-12.4>
        0
        -- </func-12.4>
    })

test:do_execsql_test(
    "func-12.5",
    [[
        CREATE TABLE t4(id integer primary key, x INT);
        INSERT INTO t4 VALUES(1, test_destructor('hello'));
        INSERT INTO t4 VALUES(2, test_destructor('world'));
        SELECT min(test_destructor(x)), max(test_destructor(x)) FROM t4;
    ]], {
        -- <func-12.5>
        "hello", "world"
        -- </func-12.5>
    })

test:do_execsql_test(
    "func-12.6",
    [[
        SELECT test_destructor_count();
    ]], {
        -- <func-12.6>
        0
        -- </func-12.6>
    })

test:do_execsql_test(
    "func-12.7",
    [[
        DROP TABLE t4;
    ]], {
        -- <func-12.7>

        -- </func-12.7>
    })

-- Test that the auxdata API for scalar functions works. This test uses
-- a special user-defined function only available in test builds,
-- test_auxdata(). Function test_auxdata() takes any number of arguments.
test:do_execsql_test(
    "func-13.1",
    [[
        SELECT test_auxdata('hello world');
    ]], {
        -- <func-13.1>
        0
        -- </func-13.1>
    })

test:do_execsql_test(
    "func-13.2",
    [[
        CREATE TABLE t4(id integer primary key, a INT, b INT);
        INSERT INTO t4 VALUES(1, 'abc', 'def');
        INSERT INTO t4 VALUES(2, 'ghi', 'jkl');
    ]], {
        -- <func-13.2>

        -- </func-13.2>
    })

test:do_execsql_test(
    "func-13.3",
    [[
        SELECT test_auxdata('hello world') FROM t4;
    ]], {
        -- <func-13.3>
        0, 1
        -- </func-13.3>
    })

test:do_execsql_test(
    "func-13.4",
    [[
        SELECT test_auxdata('hello world', 123) FROM t4;
    ]], {
        -- <func-13.4>
        "0 0", "1 1"
        -- </func-13.4>
    })

test:do_execsql_test(
    "func-13.5",
    [[
        SELECT test_auxdata('hello world', a) FROM t4;
    ]], {
        -- <func-13.5>
        "0 0", "1 0"
        -- </func-13.5>
    })

test:do_execsql_test(
    "func-13.6",
    [[
        SELECT test_auxdata('hello'||'world', a) FROM t4;
    ]], {
        -- <func-13.6>
        "0 0", "1 0"
        -- </func-13.6>
    })

-- Test that auxilary data is preserved between calls for SQL variables.
test:do_test(
    "func-13.7",
    function()
        local sql, STMT, res
        sql = "SELECT test_auxdata( ? , a ) FROM t4;"
        STMT = sql_prepare(sql, -1, "TAIL")
        sql_bind_text(STMT, 1, "hello\0", -1)
        res = {  }
        while X(690, "X!cmd", [=[["expr"," \"sql_ROW\"==[sql_step $STMT] "]]=])
 do
            table.insert(res,sql_column_text(STMT, 0))
        end
        return table.insert(res,sql_finalize(STMT)) or res
    end, {
        -- <func-13.7>
        "0 0", "1 0", "sql_OK"
        -- </func-13.7>
    })

-- Test that auxiliary data is discarded when a statement is reset.
test:do_execsql_test(
    "13.8.1",
    [[
        SELECT test_auxdata('constant') FROM t4;
    ]], {
        -- <13.8.1>
        0, 1
        -- </13.8.1>
    })

test:do_execsql_test(
    "13.8.2",
    [[
        SELECT test_auxdata('constant') FROM t4;
    ]], {
        -- <13.8.2>
        0, 1
        -- </13.8.2>
    })

db("cache", "flush")
test:do_execsql_test(
    "13.8.3",
    [[
        SELECT test_auxdata('constant') FROM t4;
    ]], {
        -- <13.8.3>
        0, 1
        -- </13.8.3>
    })

local V = "one" -- luacheck: ignore
test:do_execsql_test(
    "13.8.4",
    [[
        SELECT test_auxdata($V), $V FROM t4;
    ]], {
        -- <13.8.4>
        0, "one", 1, "one"
        -- </13.8.4>
    })

V = "two"
test:do_execsql_test(
    "13.8.5",
    [[
        SELECT test_auxdata($V), $V FROM t4;
    ]], {
        -- <13.8.5>
        0, "two", 1, "two"
        -- </13.8.5>
    })

db("cache", "flush")
V = "three"
test:do_execsql_test(
    "13.8.6",
    [[
        SELECT test_auxdata($V), $V FROM t4;
    ]], {
        -- <13.8.6>
        0, "three", 1, "three"
        -- </13.8.6>
    })

-- Make sure that a function with a very long name is rejected
test:do_test(
    "func-14.1",
    function()
        return X(723, "X!cmd", [=[["catch","\n    db function [string repeat X 254] {return \"hello\"}\n  "]]=])
    end, {
        -- <func-14.1>
        0
        -- </func-14.1>
    })

test:do_test(
    "func-14.2",
    function()
        return X(728, "X!cmd", [=[["catch","\n    db function [string repeat X 256] {return \"hello\"}\n  "]]=])
    end, {
        -- <func-14.2>
        1
        -- </func-14.2>
    })

test:do_catchsql_test(
    "func-15.1",
    [[
        select test_error(NULL)
    ]], {
        -- <func-15.1>
        1, ""
        -- </func-15.1>
    })

test:do_catchsql_test(
    "func-15.2",
    [[
        select test_error('this is the error message')
    ]], {
        -- <func-15.2>
        1, "this is the error message"
        -- </func-15.2>
    })

test:do_catchsql_test(
    "func-15.3",
    [[
        select test_error('this is the error message',12)
    ]], {
        -- <func-15.3>
        1, "this is the error message"
        -- </func-15.3>
    })

test:do_test(
    "func-15.4",
    function()
        return db("errorcode")
    end, {
        -- <func-15.4>
        12
        -- </func-15.4>
    })

-- MUST_WORK_TEST
if (0 > 0)
 then
    X(749, "X!cmd", [=[["Test","the","quote","function","for","BLOB","and","NULL","values."]]=])
end
test:do_test(
    "func-16.1",
    function()
        test:execsql([[
            CREATE TABLE tbl2(id integer primary key, a INT, b INT);
        ]])
        local STMT = sql_prepare("INSERT INTO tbl2 VALUES(1, ?, ?)", -1, "TAIL")
        sql_bind_blob(STMT, 1, "abc", 3)
        sql_step(STMT)
        sql_finalize(STMT)
        return test:execsql([[
            SELECT quote(a), quote(b) FROM tbl2;
        ]])
    end, {
        -- <func-16.1>
        "X'616263'", "NULL"
        -- </func-16.1>
    })

-- Correctly handle function error messages that include %.  Ticket #1354
--
test:do_test(
    "func-17.1",
    function()
        local function testfunc1(args) -- luacheck: no unused
            X(768, "X!cmd", [=[["error","Error %d with %s percents %p"]]=])
        end

        db("function", "testfunc1", "::testfunc1")
        return test:catchsql([[
            SELECT testfunc1(1,2,3);
        ]])
    end, {
        -- <func-17.1>
        1, "Error %d with %s percents %p"
        -- </func-17.1>
    })
end

-- The SUM function should return integer results when all inputs are integer.
--
test:do_execsql_test(
    "func-18.1",
    [[
        CREATE TABLE t5(id int primary key, x INT);
        INSERT INTO t5 VALUES(1, 1);
        INSERT INTO t5 VALUES(2, -99);
        INSERT INTO t5 VALUES(3, 10000);
        SELECT sum(x) FROM t5;
    ]], {
        -- <func-18.1>
        9902
        -- </func-18.1>
    })

test:do_execsql_test(
    "func-18.2",
    [[
        INSERT INTO t5 VALUES(4, 0.0);
        SELECT sum(x) FROM t5;
    ]], {
        -- <func-18.2>
        9902.0
        -- </func-18.2>
    })



-- The sum of nothing is NULL.  But the sum of all NULLs is NULL.
--
-- The TOTAL of nothing is 0.0.
--
test:do_execsql_test(
    "func-18.3",
    [[
        DELETE FROM t5;
        SELECT sum(x), total(x) FROM t5;
    ]], {
        -- <func-18.3>
        "", 0.0
        -- </func-18.3>
    })

test:do_execsql_test(
    "func-18.4",
    [[
        INSERT INTO t5 VALUES(1, NULL);
        SELECT sum(x), total(x) FROM t5
    ]], {
        -- <func-18.4>
        "", 0.0
        -- </func-18.4>
    })

test:do_execsql_test(
    "func-18.5",
    [[
        INSERT INTO t5 VALUES(2, NULL);
        SELECT sum(x), total(x) FROM t5
    ]], {
        -- <func-18.5>
        "", 0.0
        -- </func-18.5>
    })

test:do_execsql_test(
    "func-18.6",
    [[
        INSERT INTO t5 VALUES(3, 123);
        SELECT sum(x), total(x) FROM t5
    ]], {
        -- <func-18.6>
        123, 123.0
        -- </func-18.6>
    })

-- Ticket #1664, #1669, #1670, #1674: An integer overflow on SUM causes
-- an error. The non-standard TOTAL() function continues to give a helpful
-- result.
--
test:do_execsql_test(
    "func-18.10",
    [[
        CREATE TABLE t6(id INT primary key, x INTEGER);
        INSERT INTO t6 VALUES(1, 1);
        INSERT INTO t6 VALUES(2, 1<<62);
        SELECT sum(x) - ((1<<62)+1) from t6;
    ]], {
        -- <func-18.10>
        0
        -- </func-18.10>
    })

test:do_execsql_test(
    "func-18.11",
    [[
        SELECT typeof(sum(x)) FROM t6
    ]], {
        -- <func-18.11>
        "integer"
        -- </func-18.11>
    })

test:do_execsql_test(
    "func-18.12",
    [[
        INSERT INTO t6 VALUES(3, 1<<62);
        SELECT sum(x) - ((1<<62)*2.0+1) from t6;
    ]], {
        -- <func-18.12>
        0
        -- </func-18.12>
    })

test:do_execsql_test(
    "func-18.13",
    [[
        SELECT total(x) - ((1<<62)*2.0+1) FROM t6
    ]], {
        -- <func-18.13>
        0.0
        -- </func-18.13>
    })

test:do_execsql_test(
    "func-18.14",
    [[
        SELECT sum(-9223372036854775805);
    ]], {
        -- <func-18.14>
        -9223372036854775805LL
        -- </func-18.14>
    })
test:do_catchsql_test(
    "func-18.16",
    [[
        SELECT sum(x) FROM
           (SELECT 9223372036854775807 AS x UNION ALL
            SELECT -10 AS x);
    ]], {
    -- <func-18.16>
    0, {9223372036854775797LL}
    -- </func-18.16>
})

test:do_catchsql_test(
    "func-18.17",
    [[
        SELECT sum(x) FROM
           (SELECT -9223372036854775807 AS x UNION ALL
            SELECT 10 AS x);
    ]], {
    -- <func-18.17>
    0, {-9223372036854775797LL}
    -- </func-18.17>
})

test:do_execsql_test(
    "func-18.15.1",
    [[
        SELECT sum(x) FROM
           (SELECT 9223372036854775807 AS x UNION ALL
            SELECT 10 AS x);
    ]], {
        -- <func-18.15.1>
        9223372036854775817LL
        -- </func-18.15.1>
    })

test:do_catchsql_test(
    "func-18.15.2",
    [[
        SELECT sum(x) FROM
           (SELECT 9223372036854775807 AS x UNION ALL SELECT 9223372036854775807 AS x
            UNION ALL SELECT 10 AS x);
    ]], {
    -- <func-18.15.2>
    1, "Failed to execute SQL statement: integer overflow"
    -- </func-18.15.2>
})

test:do_catchsql_test(
    "func-18.18",
    [[
        SELECT sum(x) FROM
           (SELECT -9223372036854775807 AS x UNION ALL
            SELECT -10 AS x);
    ]], {
        -- <func-18.18>
        1, "Failed to execute SQL statement: integer overflow"
        -- </func-18.18>
    })

test:do_catchsql_test(
    "func-18.19",
    [[
        SELECT sum(x) FROM (SELECT 9 AS x UNION ALL SELECT -10 AS x);
    ]], {
        -- <func-18.19>
        0, {-1}
        -- </func-18.19>
    })

test:do_catchsql_test(
    "func-18.20",
    [[
        SELECT sum(x) FROM (SELECT -9 AS x UNION ALL SELECT 10 AS x);
    ]], {
        -- <func-18.20>
        0, {1}
        -- </func-18.20>
    })

test:do_catchsql_test(
    "func-18.21",
    [[
        SELECT sum(x) FROM (SELECT -10 AS x UNION ALL SELECT 9 AS x);
    ]], {
        -- <func-18.21>
        0, {-1}
        -- </func-18.21>
    })

test:do_catchsql_test(
    "func-18.22",
    [[
        SELECT sum(x) FROM (SELECT 10 AS x UNION ALL SELECT -9 AS x);
    ]], {
        -- <func-18.22>
        0, {1}
        -- </func-18.22>
    })



-- ifcapable compound&&subquery
-- Integer overflow on abs()
--
test:do_catchsql_test(
    "func-18.31",
    [[
        SELECT abs(-9223372036854775807);
    ]], {
        -- <func-18.31>
        0, {9223372036854775807LL}
        -- </func-18.31>
    })

test:do_catchsql_test(
    "func-18.32",
    [[
        SELECT abs(-9223372036854775807-1);
    ]], {
        -- <func-18.32>
        0, {9223372036854775808LL}
        -- </func-18.32>
    })

-- Soundex tests.
--
-- false condition for current tarantool version
for i, val in ipairs({
    {"euler", "E460"},
    {"EULER", "E460"},
    {"Euler", "E460"},
    {"ellery", "E460"},
    {"gauss", "G200"},
    {"ghosh", "G200"},
    {"hilbert", "H416"},
    {"Heilbronn", "H416"},
    {"knuth", "K530"},
    {"kant", "K530"},
    {"Lloyd", "L300"},
    {"LADD", "L300"},
    {"Lukasiewicz", "L222"},
    {"Lissajous", "L222"},
    {"A", "A000"},
    {"12345", "?000"} }) do
    local name = val[1]
    local sdx = val[2]
    test:do_execsql_test(
        "func-20."..i,
        string.format("SELECT soundex('%s')", name), {
            sdx
        })
end
-- Tests of the REPLACE function.
--
test:do_catchsql_test(
    "func-21.1",
    [[
        SELECT replace(1,2);
    ]], {
        -- <func-21.1>
        1, "Wrong number of arguments is passed to REPLACE(): expected 3, got 2"
        -- </func-21.1>
    })

test:do_catchsql_test(
    "func-21.2",
    [[
        SELECT replace(1,2,3,4);
    ]], {
        -- <func-21.2>
        1, "Wrong number of arguments is passed to REPLACE(): expected 3, got 4"
        -- </func-21.2>
    })

test:do_execsql_test(
    "func-21.3",
    [[
        SELECT typeof(replace('This is the main test string', NULL, 'ALT'));
    ]], {
        -- <func-21.3>
        "boolean"
        -- </func-21.3>
    })

test:do_execsql_test(
    "func-21.4",
    [[
        SELECT typeof(replace(NULL, 'main', 'ALT'));
    ]], {
        -- <func-21.4>
        "boolean"
        -- </func-21.4>
    })

test:do_execsql_test(
    "func-21.5",
    [[
        SELECT typeof(replace('This is the main test string', 'main', NULL));
    ]], {
        -- <func-21.5>
        "boolean"
        -- </func-21.5>
    })

test:do_execsql_test(
    "func-21.6",
    [[
        SELECT replace('This is the main test string', 'main', 'ALT');
    ]], {
        -- <func-21.6>
        "This is the ALT test string"
        -- </func-21.6>
    })

test:do_execsql_test(
    "func-21.7",
    [[
        SELECT replace('This is the main test string', 'main', 'larger-main');
    ]], {
        -- <func-21.7>
        "This is the larger-main test string"
        -- </func-21.7>
    })

test:do_execsql_test(
    "func-21.8",
    [[
        SELECT replace('aaaaaaa', 'a', '0123456789');
    ]], {
        -- <func-21.8>
        "0123456789012345678901234567890123456789012345678901234567890123456789"
        -- </func-21.8>
    })

test:do_test(
    "func-21.9",
    function()
        -- Attempt to exploit a buffer-overflow that at one time existed
        -- in the REPLACE function.
        local str = string.format("%sCC%s", string.rep("A", 29998), string.rep("A", 35537))
        local rep = string.rep("B", 65536)
        return test:execsql(string.format([[
            SELECT LENGTH(REPLACE('%s', 'C', '%s'));
        ]], str, rep))
    end, {
        -- <func-21.9>
        ((29998 + (2 * 65536)) + 35537)
        -- </func-21.9>
    })



-- Tests for the TRIM, LTRIM and RTRIM functions.
--
test:do_catchsql_test(
    "func-22.1",
    [[
        SELECT trim(1,2,3)
    ]], {
        -- <func-22.1>
        1, "Syntax error at line 1 near ','"
        -- </func-22.1>
    })

test:do_catchsql_test(
    "func-22.2",
    [[
        SELECT ltrim(1,2,3)
    ]], {
        -- <func-22.2>
        1, "Function 'LTRIM' does not exist"
        -- </func-22.2>
    })

test:do_catchsql_test(
    "func-22.3",
    [[
        SELECT rtrim(1,2,3)
    ]], {
        -- <func-22.3>
        1, "Function 'RTRIM' does not exist"
        -- </func-22.3>
    })

test:do_execsql_test(
    "func-22.4",
    [[
        SELECT trim('  hi  ');
    ]], {
        -- <func-22.4>
        "hi"
        -- </func-22.4>
    })

test:do_execsql_test(
    "func-22.5",
    [[
        SELECT TRIM(LEADING FROM '  hi  ');
    ]], {
        -- <func-22.5>
        "hi  "
        -- </func-22.5>
    })

test:do_execsql_test(
    "func-22.6",
    [[
        SELECT TRIM(TRAILING FROM '  hi  ');
    ]], {
        -- <func-22.6>
        "  hi"
        -- </func-22.6>
    })

test:do_execsql_test(
    "func-22.7",
    [[
        SELECT TRIM('xyz' FROM '  hi  ');
    ]], {
        -- <func-22.7>
        "  hi  "
        -- </func-22.7>
    })

test:do_execsql_test(
    "func-22.8",
    [[
        SELECT TRIM(LEADING 'xyz' FROM '  hi  ');
    ]], {
        -- <func-22.8>
        "  hi  "
        -- </func-22.8>
    })

test:do_execsql_test(
    "func-22.9",
    [[
        SELECT TRIM(TRAILING 'xyz' FROM '  hi  ');
    ]], {
        -- <func-22.9>
        "  hi  "
        -- </func-22.9>
    })

test:do_execsql_test(
    "func-22.10",
    [[
        SELECT TRIM('xyz' FROM 'xyxzy  hi  zzzy');
    ]], {
        -- <func-22.10>
        "  hi  "
        -- </func-22.10>
    })

test:do_execsql_test(
    "func-22.11",
    [[
        SELECT TRIM(LEADING 'xyz' FROM 'xyxzy  hi  zzzy');
    ]], {
        -- <func-22.11>
        "  hi  zzzy"
        -- </func-22.11>
    })

test:do_execsql_test(
    "func-22.12",
    [[
        SELECT TRIM(TRAILING 'xyz' FROM 'xyxzy  hi  zzzy');
    ]], {
        -- <func-22.12>
        "xyxzy  hi  "
        -- </func-22.12>
    })

test:do_execsql_test(
    "func-22.13",
    [[
        SELECT TRIM('' FROM '  hi  ');
    ]], {
        -- <func-22.13>
        "  hi  "
        -- </func-22.13>
    })

--if X(1091, "X!cmd", "[\"expr\",\"[db one {PRAGMA encoding}]==\\\"UTF-8\\\"\"]") then
test:do_execsql_test(
    "func-22.14",
    [[
        SELECT hex(TRIM(x'6162e1bfbfc280' FROM x'c280e1bfbff48fbfbf6869'))
    ]], {
        -- <func-22.14>
        "F48FBFBF6869"
        -- </func-22.14>
    })

test:do_execsql_test(
    "func-22.15",
    [[SELECT hex(TRIM(x'6162e1bfbfc280f48fbfbf'
                      FROM x'6869c280e1bfbff48fbfbf61'))]], {
        -- <func-22.15>
        "6869"
        -- </func-22.15>
    })

test:do_execsql_test(
    "func-22.16",
    [[
        SELECT hex(TRIM(x'ceb1' FROM x'ceb1ceb2ceb3'));
    ]], {
        -- <func-22.16>
        "CEB2CEB3"
        -- </func-22.16>
    })

--end
test:do_execsql_test(
    "func-22.20",
    [[
        SELECT typeof(trim(NULL));
    ]], {
        -- <func-22.20>
        "boolean"
        -- </func-22.20>
    })

test:do_execsql_test(
    "func-22.21",
    [[
        SELECT typeof(TRIM('xyz' FROM NULL));
    ]], {
        -- <func-22.21>
        "boolean"
        -- </func-22.21>
    })

test:do_execsql_test(
    "func-22.22",
    [[
        SELECT typeof(TRIM(NULL FROM 'hello'));
    ]], {
        -- <func-22.22>
        "boolean"
        -- </func-22.22>
    })

-- gh-3543 Check trimming of binary string when X'00' in trimming char set.

test:do_execsql_test(
    "func-22.23",
    [[
        SELECT TRIM(X'00' FROM X'004100');
    ]], {
        -- <func-22.23>
        "A"
        -- </func-22.23>
    })

test:do_execsql_test(
    "func-22.24",
    [[
        SELECT TRIM(X'0000' FROM X'004100');
    ]], {
        -- <func-22.24>
        "A"
        -- </func-22.24>
    })

test:do_execsql_test(
    "func-22.25",
    [[
        SELECT TRIM(X'0042' FROM X'004100');
    ]], {
        -- <func-22.25>
        "A"
        -- </func-22.25>
    })

test:do_execsql_test(
    "func-22.26",
    [[
        SELECT TRIM(X'00' FROM X'00004100420000');
    ]], {
        -- <func-22.26>
        "A\0B"
        -- </func-22.26>
    })

test:do_execsql_test(
    "func-22.27",
    [[
        SELECT TRIM(LEADING X'00' FROM X'004100');
    ]], {
        -- <func-22.27>
        "A\0"
        -- </func-22.27>
    })

test:do_execsql_test(
    "func-22.28",
    [[
        SELECT TRIM(LEADING X'0000' FROM X'004100');
    ]], {
        -- <func-22.28>
        "A\0"
        -- </func-22.28>
    })

test:do_execsql_test(
    "func-22.29",
    [[
        SELECT TRIM(LEADING X'0042' FROM X'004100');
    ]], {
        -- <func-22.29>
        "A\0"
        -- </func-22.29>
    })

test:do_execsql_test(
    "func-22.30",
    [[
        SELECT TRIM(LEADING X'00' FROM X'00004100420000');
    ]], {
        -- <func-22.30>
        "A\0B\0\0"
        -- </func-22.30>
    })

test:do_execsql_test(
    "func-22.31",
    [[
        SELECT TRIM(TRAILING X'00' FROM X'004100');
    ]], {
        -- <func-22.31>
        "\0A"
        -- </func-22.31>
    })

test:do_execsql_test(
    "func-22.32",
    [[
        SELECT TRIM(TRAILING X'0000' FROM X'004100');
    ]], {
        -- <func-22.32>
        "\0A"
        -- </func-22.32>
    })

test:do_execsql_test(
    "func-22.33",
    [[
        SELECT TRIM(TRAILING X'0042' FROM X'004100');
    ]], {
        -- <func-22.33>
        "\0A"
        -- </func-22.33>
    })

test:do_execsql_test(
    "func-22.34",
    [[
        SELECT TRIM(TRAILING X'00' FROM X'00004100420000');
    ]], {
        -- <func-22.34>
        "\0\0A\0B"
        -- </func-22.34>
    })

-- gh-3879 Check new TRIM() grammar, particularly BOTH keyword and
-- FROM without any agrs before. LEADING and TRAILING keywords is
-- checked above.

test:do_execsql_test(
    "func-22.35",
    [[
        SELECT TRIM(BOTH FROM '  hi  ');
    ]], {
        -- <func-22.35>
        "hi"
        -- </func-22.35>
    })
test:do_execsql_test(
    "func-22.36",
    [[
        SELECT TRIM(BOTH 'xyz' FROM '  hi  ');
    ]], {
        -- <func-22.36>
        "  hi  "
        -- </func-22.36>
    })

test:do_execsql_test(
    "func-22.37",
    [[
        SELECT TRIM(BOTH 'xyz' FROM 'xyxzy  hi  zzzy');
    ]], {
        -- <func-22.37>
        "  hi  "
        -- </func-22.37>
    })

test:do_catchsql_test(
    "func-22.38",
    [[
        SELECT TRIM(FROM 'xyxzy');
    ]], {
        -- <func-22.38>
        1, "Syntax error at line 1 near 'FROM'"
        -- </func-22.38>
    })

test:do_execsql_test(
    "func-22.39",
    [[
        SELECT HEX(TRIM(X'004420'))
    ]], { "4420"  })

test:do_execsql_test(
    "func-22.40",
    [[
        SELECT HEX(TRIM(X'00442000'))
    ]], { "4420"  })

test:do_execsql_test(
    "func-22.41",
    [[
        SELECT HEX(TRIM(X'442000'))
    ]], { "4420"  })

-- This is to test the deprecated sql_aggregate_count() API.
--
--test:do_test(
--    "func-23.1",
--    function()
--        sql_create_aggregate("db")
--        return test:execsql([[
--            SELECT legacy_count() FROM t6;
--        ]])
--    end, {
--        -- <func-23.1>
--        3
--        -- </func-23.1>
--    })
--


-- The group_concat() function.
--
test:do_execsql_test(
    "func-24.1",
    [[
        SELECT group_concat(t1) FROM tbl1
    ]], {
        -- <func-24.1>
        "this,program,is,free,software"
        -- </func-24.1>
    })

test:do_execsql_test(
    "func-24.2",
    [[
        SELECT group_concat(t1,' ') FROM tbl1
    ]], {
        -- <func-24.2>
        "this program is free software"
        -- </func-24.2>
    })

-- do_test func-24.3 {
--   execsql {
--     SELECT group_concat(t1,' ' || rowid || ' ') FROM tbl1
--   }
-- } {{this 2 program 3 is 4 free 5 software}}
test:do_execsql_test(
    "func-24.4",
    [[
        SELECT group_concat(NULL,t1) FROM tbl1
    ]], {
        -- <func-24.4>
        ""
        -- </func-24.4>
    })

test:do_execsql_test(
    "func-24.5",
    [[
        SELECT group_concat(t1,NULL) FROM tbl1
    ]], {
        -- <func-24.5>
        "thisprogramisfreesoftware"
        -- </func-24.5>
    })

test:do_execsql_test(
    "func-24.6",
    [[
        SELECT 'BEGIN-'||group_concat(t1) FROM tbl1
    ]], {
        -- <func-24.6>
        "BEGIN-this,program,is,free,software"
        -- </func-24.6>
    })

-- Ticket #3179:  Make sure aggregate functions can take many arguments.
-- None of the built-in aggregates do this, so use the md5sum() from the
-- test extensions.
--
-- ["unset","-nocomplain","midargs"]
local digest = require('digest')
local midargs = ""
-- ["unset","-nocomplain","midres"]
local midres = ""
-- ["unset","-nocomplain","result"]
local sql_LIMIT_FUNCTION_ARG = 6
-- MUST_WORK_TEST test should be rewritten
if 0>0 then
--for {set i 1} {$i<[sql_limit db sql_LIMIT_FUNCTION_ARG -1]} {incr i} {
for i = 1, sql_LIMIT_FUNCTION_ARG, 1 do
    midargs = midargs .. ",'/"..i.."'"
    midres = midres .. "/"..i
    local result = digest.md5_hex(string.format("this%sprogram%sis%sfree%ssoftware%s",
          midres,midres,midres,midres,midres))
    local sql = "SELECT md5sum(t1"..midargs..") FROM tbl1"
    test:do_test(
        "func-24.7."..i,
        function()
            return test:execsql(sql)
        end, {
            result
        })
end
end

-- Ticket #3806.  If the initial string in a group_concat is an empty
-- string, the separator that follows should still be present.
--
test:do_execsql_test(
    "func-24.8",
    [[
        SELECT group_concat(CASE t1 WHEN 'this' THEN '' ELSE t1 END) FROM tbl1
    ]], {
        -- <func-24.8>
        ",program,is,free,software"
        -- </func-24.8>
    })

test:do_execsql_test(
    "func-24.9",
    [[
        SELECT group_concat(CASE WHEN t1!='software' THEN '' ELSE t1 END) FROM tbl1
    ]], {
        -- <func-24.9>
        ",,,,software"
        -- </func-24.9>
    })

-- Ticket #3923.  Initial empty strings have a separator.  But initial
-- NULLs do not.
--
test:do_execsql_test(
    "func-24.10",
    [[
        SELECT group_concat(CASE t1 WHEN 'this' THEN null ELSE t1 END) FROM tbl1
    ]], {
        -- <func-24.10>
        "program,is,free,software"
        -- </func-24.10>
    })

test:do_execsql_test(
    "func-24.11",
    [[
        SELECT group_concat(CASE WHEN t1!='software' THEN null ELSE t1 END) FROM tbl1
    ]], {
        -- <func-24.11>
        "software"
        -- </func-24.11>
    })

test:do_execsql_test(
    "func-24.12",
    [[
        SELECT group_concat(CASE t1 WHEN 'this' THEN ''
                              WHEN 'program' THEN null ELSE t1 END) FROM tbl1
    ]], {
        -- <func-24.12>
        ",is,free,software"
        -- </func-24.12>
    })

-- Tests to verify ticket http://www.sql.org/src/tktview/55746f9e65f8587c0
test:do_execsql_test(
    "func-24.13",
    [[
        SELECT typeof(group_concat(x)) FROM (SELECT '' AS x);
    ]], {
        -- <func-24.13>
        "string"
        -- </func-24.13>
    })

test:do_execsql_test(
    "func-24.14",
    [[
        SELECT typeof(group_concat(x,''))
          FROM (SELECT '' AS x UNION ALL SELECT '');
    ]], {
        -- <func-24.14>
        "string"
        -- </func-24.14>
    })

-- Use the test_isolation function to make sure that type conversions
-- on function arguments do not effect subsequent arguments.
--
-- MUST_WORK_TEST
if 0>0 then
local X = nil
test:do_execsql_test(
    "func-25.1",
    [[
        SELECT test_isolation(t1,t1) FROM tbl1
    ]], {
        -- <func-25.1>
        "this", "program", "is", "free", "software"
        -- </func-25.1>
    })

-- Try to misuse the sql_create_function() interface.  Verify that
-- errors are returned.
--
test:do_test(
    "func-26.1",
    function()
        return X(1236, "X!cmd", [=[["abuse_create_function","db"]]=])
    end, {
        -- <func-26.1>

        -- </func-26.1>
    })

-- The previous test (func-26.1) registered a function with a very long
-- function name that takes many arguments and always returns NULL.  Verify
-- that this function works correctly.
--
test:do_test(
    "func-26.2",
    function()
        local a = ""
        for i in X(0, "X!for", [=[["set i 1","$i<=$::sql_MAX_FUNCTION_ARG","incr i"]]=]) do
            table.insert(a,i)
        end
        return test:execsql(string.format([[
            SELECT nullx_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789(%s);
        ]], X(1249, "X!cmd", [=[["join",["a"],","]]=])))
    end, {
        -- <func-26.2>
        ""
        -- </func-26.2>
    })

test:do_test(
    "func-26.3",
    function()
        local a = ""
        for i in X(0, "X!for", [=[["set i 1","$i<=$::sql_MAX_FUNCTION_ARG+1","incr i"]]=]) do
            table.insert(a,i)
        end
        return test:catchsql(string.format([[
            SELECT nullx_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789(%s);
        ]], X(1258, "X!cmd", [=[["join",["a"],","]]=])))
    end, {
        -- <func-26.3>
        1, "too many arguments on function nullx_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789"
        -- </func-26.3>
    })

test:do_test(
    "func-26.4",
    function()
        local a = ""
        for i in X(0, "X!for", [=[["set i 1","$i<=$::sql_MAX_FUNCTION_ARG-1","incr i"]]=]) do
            table.insert(a,i)
        end
        return test:catchsql(string.format([[
            SELECT nullx_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789(%s);
        ]], X(1267, "X!cmd", [=[["join",["a"],","]]=])))
    end, {
        -- <func-26.4>
        1, "wrong number of arguments to function nullx_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789()"
        -- </func-26.4>
    })

test:do_catchsql_test(
    "func-26.5",
    [[
        SELECT nullx_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_12345678a(0);
    ]], {
        -- <func-26.5>
        1, "no such function: nullx_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_12345678a"
        -- </func-26.5>
    })

test:do_catchsql_test(
    "func-26.6",
    [[
        SELECT nullx_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789a(0);
    ]], {
        -- <func-26.6>
        1, "no such function: nullx_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789_123456789a"
        -- </func-26.6>
    })
end

test:do_catchsql_test(
    "func-27.1",
    [[
        SELECT coalesce()
    ]], {
        -- <func-27.1>
        1, "Wrong number of arguments is passed to COALESCE(): expected at least two, got 0"
        -- </func-27.1>
    })

test:do_catchsql_test(
    "func-27.2",
    [[
        SELECT coalesce(1)
    ]], {
        -- <func-27.2>
        1, "Wrong number of arguments is passed to COALESCE(): expected at least two, got 1"
        -- </func-27.2>
    })

test:do_catchsql_test(
    "func-27.3",
    [[
        SELECT coalesce(1,2)
    ]], {
        -- <func-27.3>
        0, {1}
        -- </func-27.3>
    })

-- Ticket 2d401a94287b5
-- Unknown function in a DEFAULT expression causes a segfault.
--
test:do_test(
    "func-28.1",
    function()
        test:execsql([[
            CREATE TABLE t28(id INT primary key, x INT, y INT DEFAULT(nosuchfunc(1)));
        ]])
        return test:catchsql([[
            INSERT INTO t28(id, x) VALUES(1, 1);
        ]])
    end, {
        -- <func-28.1>
        1, "Function 'NOSUCHFUNC' does not exist"
        -- </func-28.1>
    })

-- EVIDENCE-OF: R-29701-50711 The unicode(X) function returns the numeric
-- unicode code point corresponding to the first character of the string
-- X.
--
-- EVIDENCE-OF: R-55469-62130 The char(X1,X2,...,XN) function returns a
-- string composed of characters having the unicode code point values of
-- integers X1 through XN, respectively.
--
test:do_execsql_test(
    "func-30.1",
    [[
        SELECT unicode('$');
    ]], {
        -- <func-30.1>
        36
        -- </func-30.1>
    })

test:do_execsql_test(
    "func-30.2",
    [[
        SELECT unicode('¢');
    ]], {
        -- <func-30.2>
        162
        -- </func-30.2>
    })

test:do_execsql_test(
    "func-30.3",
    [[
        SELECT unicode('€');
    ]], {
        -- <func-30.3>
        8364
        -- </func-30.3>
    })

test:do_execsql_test(
    "func-30.4",
    [[
        SELECT char(36,162,8364);
    ]], {
        -- <func-30.4>
        "$¢€"
        -- </func-30.4>
    })

for i = 1, 0xd800-1, 13 do
    test:do_execsql_test(
        "func-30.5."..i,
        "SELECT unicode(char("..i.."))", {
            i
        })

end
for i = 57344, 0xfffd, 17 do
    if (i ~= 65279) then
        test:do_execsql_test(
            "func-30.5."..i,
            "SELECT unicode(char("..i.."))", {
                i
            })
    end
end
for i = 65536, 0x10ffff, 139 do
    test:do_execsql_test(
        "func-30.5."..i,
        "SELECT unicode(char("..i.."))", {
            i
        })

end
-- Test char().
--
test:do_execsql_test(
    "func-31.1",
    [[
        SELECT char(), length(char()), typeof(char())
    ]], {
        -- <func-31.1>
        "", 0, "string"
        -- </func-31.1>
    })

test:do_execsql_test(
    "func-32",
    [[SELECT version()]],
    {_TARANTOOL})

test:do_execsql_test(
    "func-33",
    [[VALUES (ROUND(1e-31,30))]],
    {0})

test:do_execsql_test(
    "func-34",
    [[VALUES (ROUND(1e-31,31))]],
    {1e-31})

test:do_execsql_test(
    "func-35",
    [[VALUES (ROUND(1e-31, 100))]],
    {1e-31})

test:do_execsql_test(
    "func-36",
    [[VALUES (LENGTH(RANDOMBLOB(0)))]],
    {""})

-- gh-3542
-- In SQL '\0' is NOT a end-of-string signal. Tests below ensures
-- that all functions which work with strings behave this way.

-- LENGTH
test:do_execsql_test(
    "func-37",
    "SELECT LENGTH(CHAR(00));",
    {1})

test:do_execsql_test(
    "func-38",
    "SELECT LENGTH(CHAR(00, 65));",
    {2})

test:do_execsql_test(
    "func-39",
    "SELECT LENGTH(CHAR(00, 65, 00));",
    {3})

test:do_execsql_test(
    "func-40",
    "SELECT LENGTH(CHAR(00, 65, 00, 65));",
    {4})

test:do_execsql_test(
    "func-41",
    "SELECT LENGTH(CHAR(65, 00, 65, 00, 65));",
    {5})

test:do_execsql_test(
    "func-42",
    "SELECT LENGTH('ሴ' || CHAR(00) || 'ሴ');",
    {3})

-- LIKE (exact match)
test:do_execsql_test(
    "func-43",
    "SELECT CHAR(00) LIKE CHAR(00);",
    {true})

test:do_execsql_test(
    "func-44",
    "SELECT CHAR(00) LIKE CHAR(65);",
    {false})

test:do_execsql_test(
    "func-45",
    "SELECT CHAR(00,65) LIKE CHAR(00,65);",
    {true})

test:do_execsql_test(
    "func-46",
    "SELECT CHAR(00,65) LIKE CHAR(00,66);",
    {false})

test:do_execsql_test(
    "func-47",
    "SELECT CHAR(00,65, 00) LIKE CHAR(00,65,00);",
    {true})

test:do_execsql_test(
    "func-48",
    "SELECT CHAR(00,65,00) LIKE CHAR(00,65);",
    {false})

test:do_execsql_test(
    "func-49",
    "SELECT CHAR(65,00,65,00,65) LIKE CHAR(65,00,65,00,65);",
    {true})

test:do_execsql_test(
    "func-50",
    "SELECT CHAR(65,00,65,00,65) LIKE CHAR(65,00,65,00,66);",
    {false})

-- LIKE ('%' and '_')
test:do_execsql_test(
    "func-51",
    "SELECT CHAR(00) LIKE '_';",
    {true})

test:do_execsql_test(
    "func-52",
    "SELECT CHAR(00) LIKE '__';",
    {false})

test:do_execsql_test(
    "func-53",
    "SELECT CHAR(00,65) LIKE '__';",
    {true})

test:do_execsql_test(
    "func-54",
    "SELECT CHAR(00,65) LIKE '_A';",
    {true})

test:do_execsql_test(
    "func-55",
    "SELECT CHAR(00,65,00) LIKE '%';",
    {true})

test:do_execsql_test(
    "func-56",
    "SELECT CHAR(00,65,00) LIKE '%A';",
    {false})

test:do_execsql_test(
    "func-57",
    "SELECT CHAR(65,00,65,00,65) LIKE '%A';",
    {true})

test:do_execsql_test(
    "func-58",
    "SELECT CHAR(65,00,65,00,65) LIKE '%B';",
    {false})

-- LIKE (ESCAPE symbols)
test:do_execsql_test(
    "func-59",
    "SELECT CHAR(00) || '_' LIKE '_#_' ESCAPE '#';",
    {true})

test:do_execsql_test(
    "func-60",
    "SELECT CHAR(00) || '_' LIKE '_#%' ESCAPE '#';",
    {false})

-- REPLACE
test:do_execsql_test(
    "func-62",
    "SELECT REPLACE(CHAR(00,65,00,65), CHAR(00), CHAR(65));",
    {'AAAA'})

test:do_execsql_test(
    "func-63",
    "SELECT REPLACE(CHAR(00,65,00,65), CHAR(65), CHAR(00));",
    {string.char(00,00,00,00)})

-- SUBSTR
test:do_execsql_test(
    "func-64",
    "SELECT SUBSTR(CHAR(65,00,66,67), 3, 2);",
    {string.char(66,67)})

test:do_execsql_test(
    "func-65",
    "SELECT SUBSTR(CHAR(00,00,00,65), 1, 4);",
    {string.char(00,00,00,65)})

-- UPPER
test:do_execsql_test(
    "func-66",
    "SELECT UPPER(CHAR(00,97,00,98,00));",
    {string.char(00,65,00,66,00)})

-- LOWER
test:do_execsql_test(
    "func-67",
    "SELECT LOWER(CHAR(00,65,00,66,00));",
    {string.char(00,97,00,98,00)})

-- HEX
test:do_execsql_test(
    "func-68",
    "SELECT HEX(CHAR(00,65,00,65,00));",
    {'0041004100'})

-- TRIM
test:do_execsql_test(
    "func-69",
    "SELECT TRIM(CHAR(32,00,32,00,32));",
    {string.char(00,32,00)})

-- LEFT TRIM
test:do_execsql_test(
    "func-70",
    "SELECT TRIM(LEADING FROM CHAR(32,00,32,00,32));",
    {string.char(00,32,00,32)})

-- RIGHT TRIM
test:do_execsql_test(
    "func-71",
    "SELECT TRIM(TRAILING FROM CHAR(32,00,32,00,32));",
    {string.char(32,00,32,00)})

-- GROUP_CONCAT
test:do_execsql_test(
    "func-72",
    "CREATE TABLE t100(a INT PRIMARY KEY, b VARCHAR(5)); \
     INSERT INTO t100 VALUES (1, CHAR(00)); \
     INSERT INTO t100 VALUES (2, CHAR(65, 00, 65)); \
     INSERT INTO t100 VALUES (3, CHAR(00)); \
     SELECT GROUP_CONCAT(b, '') FROM t100;",
    {string.char(00,65,00,65,00)})

-- POSITION
test:do_execsql_test(
    "func-73",
    "SELECT POSITION(CHAR(65), CHAR(00,65,00,66,00));",
    {2})

test:do_execsql_test(
    "func-74",
    "SELECT POSITION(CHAR(66), CHAR(00,65,00,66,00));",
    {4})

test:do_execsql_test(
    "func-75",
    "SELECT POSITION(CHAR(00), CHAR(00,65,00,66,00));",
    {1})

test:do_execsql_test(
    "func-76",
    "SELECT POSITION(CHAR(00), CHAR(65,66));",
    {0})

-- gh-4356: Make sure that 'VARBINARY' is printed instead of the
-- binary data itself.

test:do_catchsql_test(
    "func-76.1",
    [[
        SELECT ROUND(X'FF')
    ]], {
        -- <func-76.1>
        1, "Type mismatch: can not convert varbinary(x'FF') to number"
        -- </func-76.1>
    })

test:do_catchsql_test(
    "func-76.2",
    [[
        SELECT RANDOMBLOB(X'FF')
    ]], {
        -- <func-76.2>
        1, "Type mismatch: can not convert varbinary(x'FF') to number"
        -- </func-76.2>
    })

test:do_catchsql_test(
    "func-76.3",
    [[
        SELECT SOUNDEX(X'FF')
    ]], {
        -- <func-76.3>
        1, "Type mismatch: can not convert varbinary(x'FF') to string"
        -- </func-76.3>
    })

test:do_catchsql_test(
    "func-76.4",
    [[
        SELECT SUM(X'FF')
    ]], {
        -- <func-76.4>
        1, "Type mismatch: can not convert varbinary(x'FF') to number"
        -- </func-76.4>
    })

_G.ffi = require('ffi')
box.schema.func.create('TOSTRING', {language = 'Lua', is_deterministic = true,
                                    body = 'function(a) return a end',
                                    param_list = {'any'}, returns = 'string',
                                    exports = {'LUA', 'SQL'}})
test:do_execsql_test("func-77.1", "SELECT TOSTRING('1');", {'1'})
test:do_execsql_test("func-77.2", "SELECT TOSTRING('a');", {'a'})
test:do_catchsql_test("func-77.3", "SELECT TOSTRING(1);", {1, "Function 'TOSTRING' returned value of invalid type: expected string got unsigned"})
test:do_catchsql_test("func-77.4", "SELECT TOSTRING(-1);", {1, "Function 'TOSTRING' returned value of invalid type: expected string got integer"})
test:do_catchsql_test("func-77.5", "SELECT TOSTRING(-1.1);", {1, "Function 'TOSTRING' returned value of invalid type: expected string got double"})
test:do_catchsql_test("func-77.6", "SELECT TOSTRING(TRUE);", {1, "Function 'TOSTRING' returned value of invalid type: expected string got boolean"})
test:do_execsql_test("func-77.7", "SELECT TOSTRING(NULL);", {""})
test:do_execsql_test("func-77.8", "SELECT TOSTRING(LUA('return nil'));", {""})
test:do_execsql_test("func-77.9", "SELECT TOSTRING(LUA('return box.NULL'));", {""})
test:do_catchsql_test("func-77.10", "SELECT TOSTRING(LUA('return ffi.new(\"unsigned\", 666)'));", {1, "Function 'TOSTRING' returned value of invalid type: expected string got unsigned"})
test:do_catchsql_test("func-77.11", "SELECT TOSTRING(LUA('return ffi.new(\"int\", -666)'));", {1, "Function 'TOSTRING' returned value of invalid type: expected string got integer"})
test:do_catchsql_test("func-77.12", "SELECT TOSTRING(LUA('return ffi.new(\"double\", -666.666)'));", {1, "Function 'TOSTRING' returned value of invalid type: expected string got double"})
test:do_catchsql_test("func-77.14", "SELECT TOSTRING(LUA('return ffi.new(\"bool\", true)'));", {1, "Function 'TOSTRING' returned value of invalid type: expected string got boolean"})
test:do_execsql_test("func-77.13", "SELECT TOSTRING(LUA('return ffi.string(\"hello\", 5)'));", {'hello'})
box.func.TOSTRING:drop()

box.schema.func.create('TOUNSIGNED', {language = 'Lua', is_deterministic = true,
                                      body = 'function(a) return a end',
                                      param_list = {'any'}, returns = 'unsigned',
                                      exports = {'LUA', 'SQL'}})
test:do_catchsql_test("func-78.1", "SELECT TOUNSIGNED('1');", {1, "Function 'TOUNSIGNED' returned value of invalid type: expected unsigned got string"})
test:do_catchsql_test("func-78.2", "SELECT TOUNSIGNED('a');", {1, "Function 'TOUNSIGNED' returned value of invalid type: expected unsigned got string"})
test:do_execsql_test("func-78.3", "SELECT TOUNSIGNED(1);", {1})
test:do_catchsql_test("func-78.4", "SELECT TOUNSIGNED(-1);", {1, "Function 'TOUNSIGNED' returned value of invalid type: expected unsigned got integer"})
test:do_catchsql_test("func-78.5", "SELECT TOUNSIGNED(-1.1);", {1, "Function 'TOUNSIGNED' returned value of invalid type: expected unsigned got double"})
test:do_catchsql_test("func-78.6", "SELECT TOUNSIGNED(TRUE);", {1, "Function 'TOUNSIGNED' returned value of invalid type: expected unsigned got boolean"})
test:do_execsql_test("func-78.7", "SELECT TOUNSIGNED(NULL);", {""})
test:do_execsql_test("func-78.8", "SELECT TOUNSIGNED(LUA('return nil'));", {""})
test:do_execsql_test("func-78.9", "SELECT TOUNSIGNED(LUA('return box.NULL'));", {""})
test:do_execsql_test("func-78.10", "SELECT TOUNSIGNED(LUA('return ffi.new(\"unsigned\", 666)'));", {666})
test:do_catchsql_test("func-78.11", "SELECT TOUNSIGNED(LUA('return ffi.new(\"int\", -666)'));", {1, "Function 'TOUNSIGNED' returned value of invalid type: expected unsigned got integer"})
test:do_catchsql_test("func-78.12", "SELECT TOUNSIGNED(LUA('return ffi.new(\"double\", -666.666)'));", {1, "Function 'TOUNSIGNED' returned value of invalid type: expected unsigned got double"})
test:do_catchsql_test("func-78.14", "SELECT TOUNSIGNED(LUA('return ffi.new(\"bool\", true)'));", {1, "Function 'TOUNSIGNED' returned value of invalid type: expected unsigned got boolean"})
test:do_catchsql_test("func-78.13", "SELECT TOUNSIGNED(LUA('return ffi.string(\"hello\", 5)'));", {1, "Function 'TOUNSIGNED' returned value of invalid type: expected unsigned got string"})
box.func.TOUNSIGNED:drop()

box.schema.func.create('TOINTEGER', {language = 'Lua', is_deterministic = true,
                                     body = 'function(a) return a end',
                                     param_list = {'any'}, returns = 'integer',
                                     exports = {'LUA', 'SQL'}})
test:do_catchsql_test("func-79.1", "SELECT TOINTEGER('1');", {1, "Function 'TOINTEGER' returned value of invalid type: expected integer got string"})
test:do_catchsql_test("func-79.2", "SELECT TOINTEGER('a');", {1, "Function 'TOINTEGER' returned value of invalid type: expected integer got string"})
test:do_execsql_test("func-79.3", "SELECT TOINTEGER(1);", {1})
test:do_execsql_test("func-79.4", "SELECT TOINTEGER(-1);", {-1})
test:do_catchsql_test("func-79.5", "SELECT TOINTEGER(-1.1);", {1, "Function 'TOINTEGER' returned value of invalid type: expected integer got double"})
test:do_catchsql_test("func-79.6", "SELECT TOINTEGER(TRUE);", {1, "Function 'TOINTEGER' returned value of invalid type: expected integer got boolean"})
test:do_execsql_test("func-79.7", "SELECT TOINTEGER(NULL);", {""})
test:do_execsql_test("func-79.8", "SELECT TOINTEGER(LUA('return nil'));", {""})
test:do_execsql_test("func-79.9", "SELECT TOINTEGER(LUA('return box.NULL'));", {""})
test:do_execsql_test("func-79.10", "SELECT TOINTEGER(LUA('return ffi.new(\"unsigned\", 666)'));", {666})
test:do_execsql_test("func-79.11", "SELECT TOINTEGER(LUA('return ffi.new(\"int\", -666)'));", {-666})
test:do_catchsql_test("func-79.12", "SELECT TOINTEGER(LUA('return ffi.new(\"double\", -666.666)'));", {1, "Function 'TOINTEGER' returned value of invalid type: expected integer got double"})
test:do_catchsql_test("func-79.14", "SELECT TOINTEGER(LUA('return ffi.new(\"bool\", true)'));", {1, "Function 'TOINTEGER' returned value of invalid type: expected integer got boolean"})
test:do_catchsql_test("func-79.13", "SELECT TOINTEGER(LUA('return ffi.string(\"hello\", 5)'));", {1, "Function 'TOINTEGER' returned value of invalid type: expected integer got string"})
box.func.TOINTEGER:drop()

box.schema.func.create('TONUMBER', {language = 'Lua', is_deterministic = true,
                                    body = 'function(a) return a end',
                                    param_list = {'any'}, returns = 'number',
                                    exports = {'LUA', 'SQL'}})
test:do_catchsql_test("func-80.1", "SELECT TONUMBER('1');", {1, "Function 'TONUMBER' returned value of invalid type: expected number got string"})
test:do_catchsql_test("func-80.2", "SELECT TONUMBER('a');", {1, "Function 'TONUMBER' returned value of invalid type: expected number got string"})
test:do_execsql_test("func-80.3", "SELECT TONUMBER(1);", {1})
test:do_execsql_test("func-80.4", "SELECT TONUMBER(-1);", {-1})
test:do_execsql_test("func-80.5", "SELECT TONUMBER(-1.1);", {-1.1})
test:do_catchsql_test("func-80.6", "SELECT TONUMBER(TRUE);", {1, "Function 'TONUMBER' returned value of invalid type: expected number got boolean"})
test:do_execsql_test("func-80.7", "SELECT TONUMBER(NULL);", {""})
test:do_execsql_test("func-80.8", "SELECT TONUMBER(LUA('return nil'));", {""})
test:do_execsql_test("func-80.9", "SELECT TONUMBER(LUA('return box.NULL'));", {""})
test:do_execsql_test("func-80.10", "SELECT TONUMBER(LUA('return ffi.new(\"unsigned\", 666)'));", {666})
test:do_execsql_test("func-80.11", "SELECT TONUMBER(LUA('return ffi.new(\"int\", -666)'));", {-666})
test:do_execsql_test("func-80.12", "SELECT TONUMBER(LUA('return ffi.new(\"double\", -666.666)'));", {-666.666})
test:do_catchsql_test("func-80.14", "SELECT TONUMBER(LUA('return ffi.new(\"bool\", true)'));", {1, "Function 'TONUMBER' returned value of invalid type: expected number got boolean"})
test:do_catchsql_test("func-80.13", "SELECT TONUMBER(LUA('return ffi.string(\"hello\", 5)'));", {1, "Function 'TONUMBER' returned value of invalid type: expected number got string"})
box.func.TONUMBER:drop()

box.schema.func.create('TOBOOLEAN', {language = 'Lua', is_deterministic = true,
                                    body = 'function(a) return a end',
                                    param_list = {'any'}, returns = 'boolean',
                                    exports = {'LUA', 'SQL'}})
test:do_catchsql_test("func-81.1", "SELECT TOBOOLEAN('1');", {1, "Function 'TOBOOLEAN' returned value of invalid type: expected boolean got string"})
test:do_catchsql_test("func-81.2", "SELECT TOBOOLEAN('a');", {1, "Function 'TOBOOLEAN' returned value of invalid type: expected boolean got string"})
test:do_catchsql_test("func-81.3", "SELECT TOBOOLEAN(1);", {1, "Function 'TOBOOLEAN' returned value of invalid type: expected boolean got unsigned"})
test:do_catchsql_test("func-81.4", "SELECT TOBOOLEAN(-1);", {1, "Function 'TOBOOLEAN' returned value of invalid type: expected boolean got integer"})
test:do_catchsql_test("func-81.5", "SELECT TOBOOLEAN(-1.1);", {1, "Function 'TOBOOLEAN' returned value of invalid type: expected boolean got double"})
test:do_execsql_test("func-81.6", "SELECT TOBOOLEAN(TRUE);", {true})
test:do_execsql_test("func-81.7", "SELECT TOBOOLEAN(NULL);", {""})
test:do_execsql_test("func-81.8", "SELECT TOBOOLEAN(LUA('return nil'));", {""})
test:do_execsql_test("func-81.9", "SELECT TOBOOLEAN(LUA('return box.NULL'));", {""})
test:do_catchsql_test("func-81.10", "SELECT TOBOOLEAN(LUA('return ffi.new(\"unsigned\", 666)'));", {1, "Function 'TOBOOLEAN' returned value of invalid type: expected boolean got unsigned"})
test:do_catchsql_test("func-81.11", "SELECT TOBOOLEAN(LUA('return ffi.new(\"int\", -666)'));", {1, "Function 'TOBOOLEAN' returned value of invalid type: expected boolean got integer"})
test:do_catchsql_test("func-81.12", "SELECT TOBOOLEAN(LUA('return ffi.new(\"double\", -666.666)'));", {1, "Function 'TOBOOLEAN' returned value of invalid type: expected boolean got double"})
test:do_execsql_test("func-81.14", "SELECT TOBOOLEAN(LUA('return ffi.new(\"bool\", true)'));", {true})
test:do_catchsql_test("func-81.13", "SELECT TOBOOLEAN(LUA('return ffi.string(\"hello\", 5)'));", {1, "Function 'TOBOOLEAN' returned value of invalid type: expected boolean got string"})
box.func.TOBOOLEAN:drop()

box.schema.func.create('TOSCALAR', {language = 'Lua', is_deterministic = true,
                                    body = 'function(a) return a end',
                                    param_list = {'any'}, returns = 'scalar',
                                    exports = {'LUA', 'SQL'}})
test:do_execsql_test("func-82.1", "SELECT TOSCALAR('1');", {'1'})
test:do_execsql_test("func-82.2", "SELECT TOSCALAR('a');", {'a'})
test:do_execsql_test("func-82.3", "SELECT TOSCALAR(1);", {1})
test:do_execsql_test("func-82.4", "SELECT TOSCALAR(-1);", {-1})
test:do_execsql_test("func-82.5", "SELECT TOSCALAR(-1.1);", {-1.1})
test:do_execsql_test("func-82.6", "SELECT TOSCALAR(TRUE);", {true})
test:do_execsql_test("func-82.7", "SELECT TOSCALAR(NULL);", {""})
test:do_execsql_test("func-82.8", "SELECT TOSCALAR(LUA('return nil'));", {""})
test:do_execsql_test("func-82.9", "SELECT TOSCALAR(LUA('return box.NULL'));", {""})
test:do_execsql_test("func-82.10", "SELECT TOSCALAR(LUA('return ffi.new(\"unsigned\", 666)'));", {666})
test:do_execsql_test("func-82.11", "SELECT TOSCALAR(LUA('return ffi.new(\"int\", -666)'));", {-666})
test:do_execsql_test("func-82.12", "SELECT TOSCALAR(LUA('return ffi.new(\"double\", -666.666)'));", {-666.666})
test:do_execsql_test("func-82.14", "SELECT TOSCALAR(LUA('return ffi.new(\"bool\", true)'));", {true})
test:do_execsql_test("func-82.13", "SELECT TOSCALAR(LUA('return ffi.string(\"hello\", 5)'));", {"hello"})
box.func.TOSCALAR:drop()

test:finish_test()
