#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(118)

--!./tcltestrunner.lua
-- 2008 April 1
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
-- Test cases designed to exercise and verify the logic for
-- factoring constant expressions out of loops and for
-- common subexpression eliminations.
--
-- $Id: cse.test,v 1.6 2008/08/04 03:51:24 danielk1977 Exp $
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_test(
    "cse-1.1",
    function()
        test:execsql [[
            CREATE TABLE t1(a INTEGER PRIMARY KEY, b INT , c INT , d INT , e INT , f INT );
            INSERT INTO t1 VALUES(1,11,12,13,14,15);
            INSERT INTO t1 VALUES(2,21,22,23,24,25);
        ]]
        return test:execsql [[
            SELECT b, -b, ~b, NOT CAST(b AS BOOLEAN), NOT NOT CAST(b AS BOOLEAN), b-b, b+b, b*b, b/b, b FROM t1
        ]]
    end, {
        -- <cse-1.1>
        11, -11, -12, false, true, 0, 22, 121, 1, 11, 21, -21, -22, false, true, 0, 42, 441, 1, 21
        -- </cse-1.1>
    })

test:do_execsql_test(
    "cse-1.2",
    [[
        SELECT b, b%b, b==b, b!=b, b<b, b<=b, b IS NULL, b IS NOT NULL, b FROM t1
    ]], {
        -- <cse-1.2>
        11, 0, true, false, false, true, false, true, 11, 21, 0, true, false, false, true, false, true, 21
        -- </cse-1.2>
    })

test:do_execsql_test(
    "cse-1.3",
    [[
        SELECT b, abs(b), coalesce(b,-b,NOT b,c,NOT c), c, -c FROM t1;
    ]], {
        -- <cse-1.3>
        11, 11, 11, 12, -12, 21, 21, 21, 22, -22
        -- </cse-1.3>
    })

test:do_execsql_test(
    "cse-1.4",
    [[
        SELECT CASE WHEN a==1 THEN b ELSE c END, b, c FROM t1
    ]], {
        -- <cse-1.4>
        11, 11, 12, 22, 21, 22
        -- </cse-1.4>
    })

test:do_execsql_test(
    "cse-1.5",
    [[
        SELECT CASE a WHEN 1 THEN b WHEN 2 THEN c ELSE d END, b, c, d FROM t1
    ]], {
        -- <cse-1.5>
        11, 11, 12, 13, 22, 21, 22, 23
        -- </cse-1.5>
    })

test:do_execsql_test(
    "cse-1.6.1",
    [[
        SELECT CASE b WHEN 11 THEN -b WHEN 21 THEN -c ELSE -d END, b, c, d FROM t1
    ]], {
        -- <cse-1.6.1>
        -11, 11, 12, 13, -22, 21, 22, 23
        -- </cse-1.6.1>
    })

test:do_execsql_test(
    "cse-1.6.2",
    [[
        SELECT CASE b+1 WHEN c THEN d WHEN e THEN f ELSE 999 END, b, c, d FROM t1
    ]], {
        -- <cse-1.6.2>
        13, 11, 12, 13, 23, 21, 22, 23
        -- </cse-1.6.2>
    })

test:do_execsql_test(
    "cse-1.6.3",
    [[
        SELECT CASE WHEN CAST(b AS BOOLEAN) THEN d WHEN CAST(e AS BOOLEAN)  THEN f ELSE 999 END, b, c, d FROM t1
    ]], {
        -- <cse-1.6.3>
        13, 11, 12, 13, 23, 21, 22, 23
        -- </cse-1.6.3>
    })

test:do_execsql_test(
    "cse-1.6.4",
    [[
        SELECT b, c, d, CASE WHEN CAST(b AS BOOLEAN) THEN d WHEN CAST(e AS BOOLEAN) THEN f ELSE 999 END FROM t1
    ]], {
        -- <cse-1.6.4>
        11, 12, 13, 13, 21, 22, 23, 23
        -- </cse-1.6.4>
    })

test:do_execsql_test(
    "cse-1.6.5",
    [[
        SELECT b, c, d, CASE WHEN false THEN d WHEN CAST(e AS BOOLEAN) THEN f ELSE 999 END FROM t1
    ]], {
        -- <cse-1.6.5>
        11, 12, 13, 15, 21, 22, 23, 25
        -- </cse-1.6.5>
    })

test:do_execsql_test(
    "cse-1.7",
    [[
        SELECT a, -a, ~a, NOT CAST(a AS BOOLEAN), NOT NOT CAST(a AS BOOLEAN), a-a, a+a, a*a, a/a, a FROM t1
    ]], {
        -- <cse-1.7>
        1, -1 ,-2, false, true, 0, 2, 1, 1, 1, 2, -2, -3, false, true, 0, 4, 4, 1, 2
        -- </cse-1.7>
    })

test:do_execsql_test(
    "cse-1.8",
    [[
        SELECT a, a%a, a==a, a!=a, a<a, a<=a, a IS NULL, a IS NOT NULL, a FROM t1
    ]], {
        -- <cse-1.8>
        1, 0, true, false, false, true, false, true, 1, 2, 0, true, false, false, true, false, true, 2
        -- </cse-1.8>
    })

test:do_execsql_test(
    "cse-1.9",
    [[
        SELECT NOT CAST(b AS BOOLEAN), ~b, NOT NOT CAST(b AS BOOLEAN), b FROM t1
    ]], {
        -- <cse-1.9>
        false, -12, true, 11, false, -22, true, 21
        -- </cse-1.9>
    })

test:do_execsql_test(
    "cse-1.10",
    [[
        SELECT CAST(b AS integer), typeof(b), CAST(b AS text), typeof(b) FROM t1
    ]], {
        -- <cse-1.10>
        11, "integer", "11", "integer", 21, "integer", "21", "integer"
        -- </cse-1.10>
    })

test:do_execsql_test(
    "cse-1.11",
    [[
        SELECT *,* FROM t1 WHERE a=2
        UNION ALL
        SELECT *,* FROM t1 WHERE a=1
    ]], {
        -- <cse-1.11>
        2, 21, 22, 23, 24, 25, 2, 21, 22, 23, 24, 25, 1, 11, 12, 13, 14, 15, 1, 11, 12, 13, 14, 15
        -- </cse-1.11>
    })

test:do_execsql_test(
    "cse-1.12",
    [[
        SELECT coalesce(b,c,d,e), a, b, c, d, e FROM t1 WHERE a=2
        UNION ALL
        SELECT coalesce(e,d,c,b), e, d, c, b, a FROM t1 WHERE a=1
    ]], {
        -- <cse-1.12>
        21, 2, 21, 22, 23, 24, 14, 14, 13, 12, 11, 1
        -- </cse-1.12>
    })



test:do_execsql_test(
    "cse-1.13",
    [[
        SELECT upper(b), typeof(b), b FROM t1
    ]], {
        -- <cse-1.13>
        "11", "integer", 11, "21", "integer", 21
        -- </cse-1.13>
    })

test:do_execsql_test(
    "cse-1.14",
    [[
        SELECT b, typeof(b), upper(b), typeof(b), b FROM t1
    ]], {
        -- <cse-1.14>
        11, "integer", "11", "integer", 11, 21, "integer", "21", "integer", 21
        -- </cse-1.14>
    })

-- Overflow the column cache.  Create queries involving more and more
-- columns until the cache overflows.  Verify correct operation throughout.
--
test:do_execsql_test(
    "cse-2.1",
    [[
        CREATE TABLE t2(a0  INT PRIMARY KEY,a1 INT ,a2 INT ,a3 INT ,a4 INT ,a5 INT ,a6 INT ,a7 INT ,a8 INT ,a9 INT ,
                        a10 INT ,a11 INT ,a12 INT ,a13 INT ,a14 INT ,a15 INT ,a16 INT ,a17 INT ,a18 INT ,a19 INT ,
                        a20 INT ,a21 INT ,a22 INT ,a23 INT ,a24 INT ,a25 INT ,a26 INT ,a27 INT ,a28 INT ,a29 INT ,
                        a30 INT ,a31 INT ,a32 INT ,a33 INT ,a34 INT ,a35 INT ,a36 INT ,a37 INT ,a38 INT ,a39 INT ,
                        a40 INT ,a41 INT ,a42 INT ,a43 INT ,a44 INT ,a45 INT ,a46 INT ,a47 INT ,a48 INT ,a49 INT );
        INSERT INTO t2 VALUES(0,1,2,3,4,5,6,7,8,9,
                        10,11,12,13,14,15,16,17,18,19,
                        20,21,22,23,24,25,26,27,28,29,
                        30,31,32,33,34,35,36,37,38,39,
                        40,41,42,43,44,45,46,47,48,49);
        SELECT * FROM t2;
    ]], {
        -- <cse-2.1>
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49
        -- </cse-2.1>
    })

for i = 1, 99, 1 do
    local n = (math.floor((math.random() * 44)) + 5)
    local colset = {}
    local answer = {}
    for j = 0, n, 1 do
        local r = (j + math.floor((math.random() * 5)))
        if (r > 49) then
            r = (99 - r)
        end
        table.insert(colset,"a"..j)
        table.insert(colset,"a"..r)
        table.insert(answer,j)
        table.insert(answer,r)
    end
    local sql = "SELECT "..table.concat(colset, ",").." FROM t2"
    test:do_test(
        "cse-2.2."..i,
        function()
            -- explain $::sql
            return test:execsql(sql)
        end, answer)

end


test:finish_test()
