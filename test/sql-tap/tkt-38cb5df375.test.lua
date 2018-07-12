#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(293)

--!./tcltestrunner.lua
-- 2010 October 6
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library. Specifically,
-- it tests that ticket [38cb5df375078d3f9711482d2a1615d09f6b3f33] has
-- been resolved.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

local function lrange(arr, start_num, end_num)
    local tmp = {}
    start_num = start_num + 1
    end_num = end_num + 1
    end_num = end_num < #arr and end_num or #arr
    for i = start_num, end_num, 1 do
        table.insert(tmp, arr[i])
    end
    return tmp
end

local ii
test:do_execsql_test(
    "tkt-38cb5df375.0",
    [[
        CREATE TABLE t1(a  INT primary key);
        INSERT INTO t1 VALUES(1);
        INSERT INTO t1 VALUES(2);
        INSERT INTO t1 SELECT a+2 FROM t1;
        INSERT INTO t1 SELECT a+4 FROM t1;
    ]], {
        -- <tkt-38cb5df375.0>
        
        -- </tkt-38cb5df375.0>
    })

for ii = 1, 16, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.1."..ii,
        [[
            SELECT * FROM (SELECT * FROM t1 ORDER BY a)
            UNION ALL SELECT 9 FROM (SELECT a FROM t1)
            LIMIT ]]..ii, lrange({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 9, 9, 9, 9, 9, 9 }, 0 ,  ii-1 ))

end
for ii = 1, 16, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.2."..ii,
        [[
            SELECT 9 FROM (SELECT * FROM t1)
            UNION ALL SELECT a FROM (SELECT a FROM t1 ORDER BY a)
            LIMIT ]]..ii, lrange({ 9, 9, 9, 9, 9, 9, 9, 9, 1, 2, 3, 4, 5, 6, 7, 8 }, 0 ,  ii-1 ))

end
for ii = 1, 16, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.3."..ii,
        [[
            SELECT a FROM (SELECT * FROM t1 ORDER BY a)
            UNION ALL SELECT a FROM (SELECT a FROM t1 ORDER BY a)
            LIMIT ]]..ii, lrange({ 1, 2, 3, 4, 5, 6, 7, 8, 1, 2, 3, 4, 5, 6, 7, 8 }, 0 ,  ii-1 ))

end
for ii = 1, 16, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.4."..ii,
        [[
            SELECT 0 FROM (SELECT * FROM t1)
            UNION ALL SELECT 9 FROM (SELECT a FROM t1)
            LIMIT ]]..ii, lrange({ 0, 0, 0, 0, 0, 0, 0, 0, 9, 9, 9, 9, 9, 9, 9, 9 }, 0 ,  ii-1 ))

end
for ii = 1, 4, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.5."..ii,
        [[
            SELECT 0 FROM (SELECT * FROM t1)
            UNION SELECT 9 FROM (SELECT a FROM t1)
            LIMIT ]]..ii, lrange({ 0, 9 }, 0 ,  ii-1 ))

end
for ii = 1, 11, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.11."..ii,
        [[
            SELECT * FROM (SELECT * FROM t1 ORDER BY a LIMIT 3)
            UNION ALL SELECT 9 FROM (SELECT a FROM t1)
            LIMIT ]]..ii, lrange({ 1, 2, 3, 9, 9, 9, 9, 9, 9, 9, 9 }, 0 ,  ii-1 ))

end
for ii = 1, 11, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.12."..ii,
        [[
            SELECT 9 FROM (SELECT * FROM t1)
            UNION ALL SELECT a FROM (SELECT a FROM t1 ORDER BY a LIMIT 3)
            LIMIT ]]..ii, lrange({ 9, 9, 9, 9, 9, 9, 9, 9, 1, 2, 3 }, 0 ,  ii-1 ))

end
for ii = 1, 6, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.13."..ii,
        [[
            SELECT a FROM (SELECT * FROM t1 ORDER BY a LIMIT 3)
            UNION ALL SELECT a FROM (SELECT a FROM t1 ORDER BY a LIMIT 3)
            LIMIT ]]..ii, lrange({ 1, 2, 3, 1, 2, 3 }, 0 ,  ii-1 ))

end
for ii = 1, 6, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.14."..ii,
        [[
            SELECT 0 FROM (SELECT * FROM t1 LIMIT 3)
            UNION ALL SELECT 9 FROM (SELECT a FROM t1 LIMIT 3)
            LIMIT ]]..ii, lrange({ 0, 0, 0, 9, 9, 9 }, 0 ,  ii-1 ))

end
for ii = 1, 16, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.21."..ii,
        [[
            SELECT * FROM (SELECT * FROM t1 ORDER BY a)
            UNION ALL SELECT 9 FROM (SELECT a FROM t1)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 9, 9, 9, 9, 9, 9 }, 0 ,  ii-1 ))

end
for ii = 1, 16, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.22."..ii,
        [[
            SELECT 9 FROM (SELECT * FROM t1)
            UNION ALL SELECT a FROM (SELECT a FROM t1 ORDER BY a)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 9, 9, 9, 9, 9, 9 }, 0 ,  ii-1 ))

end
for ii = 1, 16, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.23."..ii,
        [[
            SELECT a FROM (SELECT * FROM t1 ORDER BY a)
            UNION ALL SELECT a FROM (SELECT a FROM t1 ORDER BY a)
            ORDER BY 1 DESC
            LIMIT ]]..ii, lrange({ 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 1, 1 }, 0 ,  ii-1 ))

end
for ii = 1, 16, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.24."..ii,
        [[
            SELECT 0 FROM (SELECT * FROM t1)
            UNION ALL SELECT 9 FROM (SELECT a FROM t1)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 0, 0, 0, 0, 0, 0, 0, 0, 9, 9, 9, 9, 9, 9, 9, 9 }, 0 ,  ii-1 ))

end
for ii = 1, 11, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.31."..ii,
        [[
            SELECT * FROM (SELECT * FROM t1 ORDER BY a LIMIT 3)
            UNION ALL SELECT 9 FROM (SELECT a FROM t1)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 1, 2, 3, 9, 9, 9, 9, 9, 9, 9, 9 }, 0 ,  ii-1 ))

end
for ii = 1, 11, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.32."..ii,
        [[
            SELECT 9 FROM (SELECT * FROM t1)
            UNION ALL SELECT a FROM (SELECT a FROM t1 ORDER BY a LIMIT 3)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 1, 2, 3, 9, 9, 9, 9, 9, 9, 9, 9 }, 0 ,  ii-1 ))

end
for ii = 1, 7, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.33."..ii,
        [[
            SELECT a FROM (SELECT * FROM t1 ORDER BY a LIMIT 4)
            UNION ALL SELECT 90+a FROM (SELECT a FROM t1 ORDER BY a LIMIT 3)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 1, 2, 3, 4, 91, 92, 93 }, 0 ,  ii-1 ))

end
for ii = 1, 7, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.34."..ii,
        [[
            SELECT a FROM (SELECT * FROM t1 ORDER BY a LIMIT 2)
            UNION ALL SELECT a FROM (SELECT a FROM t1 ORDER BY a LIMIT 5)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 1, 1, 2, 2, 3, 4, 5 }, 0 ,  ii-1 ))

end
for ii = 1, 7, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.35."..ii,
        [[
            SELECT a FROM (SELECT * FROM t1 ORDER BY a LIMIT 5)
            UNION ALL SELECT a FROM (SELECT a FROM t1 ORDER BY a LIMIT 2)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 1, 1, 2, 2, 3, 4, 5 }, 0 ,  ii-1 ))

end
for ii = 1, 7, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.35b."..ii,
        [[
            SELECT a FROM (SELECT * FROM t1 ORDER BY a LIMIT 5)
            UNION ALL SELECT a+10 FROM (SELECT a FROM t1 ORDER BY a LIMIT 2)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 1, 2, 3, 4, 5, 11, 12 }, 0 ,  ii-1 ))

end
for ii = 1, 7, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.35c."..ii,
        [[
            SELECT a FROM (SELECT * FROM t1 ORDER BY a LIMIT 5)
            UNION SELECT a+10 FROM (SELECT a FROM t1 ORDER BY a LIMIT 2)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 1, 2, 3, 4, 5, 11, 12 }, 0 ,  ii-1 ))

end
for ii = 1, 7, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.35d."..ii,
        [[
            SELECT a FROM (SELECT * FROM t1 ORDER BY a LIMIT 5)
            INTERSECT SELECT a FROM (SELECT a FROM t1 ORDER BY a LIMIT 2)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 1, 2 }, 0 ,  ii-1 ))

end
for ii = 1, 7, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.35e."..ii,
        [[
            SELECT a FROM (SELECT * FROM t1 ORDER BY a LIMIT 5)
            EXCEPT SELECT a FROM (SELECT a FROM t1 ORDER BY a LIMIT 2)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 3, 4, 5 }, 0 ,  ii-1 ))

end
for ii = 1, 7, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.36."..ii,
        [[
            SELECT 0 FROM (SELECT * FROM t1 LIMIT 3)
            UNION ALL SELECT 9 FROM (SELECT a FROM t1 LIMIT 4)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 0, 0, 0, 9, 9, 9, 9 }, 0 ,  ii-1 ))

end
for ii = 1, 7, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.37."..ii,
        [[
            SELECT 0 FROM (SELECT * FROM t1 LIMIT 3)
            UNION SELECT 9 FROM (SELECT a FROM t1 LIMIT 4)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 0, 9 }, 0 ,  ii-1 ))

end
for ii = 1, 7, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.38."..ii,
        [[
            SELECT 0 FROM (SELECT * FROM t1 LIMIT 3)
            EXCEPT SELECT 9 FROM (SELECT a FROM t1 LIMIT 4)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 0 }, 0 ,  ii-1 ))

end
for ii = 1, 9, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.41."..ii,
        [[
            SELECT 0 FROM (SELECT * FROM t1 LIMIT 3)
            UNION ALL SELECT 9 FROM (SELECT a FROM t1 LIMIT 4)
            UNION ALL SELECT 88 FROM (SELECT a FROM t1 LIMIT 2)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 0, 0, 0, 9, 9, 9, 9, 88, 88 }, 0 ,  ii-1 ))

end
for ii = 1, 9, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.42."..ii,
        [[
            SELECT a FROM (SELECT * FROM t1 ORDER BY a LIMIT 3)
            UNION ALL SELECT a+10 FROM (SELECT a FROM t1 ORDER BY a LIMIT 4)
            UNION ALL SELECT a+20 FROM (SELECT a FROM t1 ORDER BY a LIMIT 2)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 1, 2, 3, 11, 12, 13, 14, 21, 22 }, 0 ,  ii-1 ))

end
for ii = 1, 9, 1 do
    test:do_execsql_test(
        "tkt-38cb5df375.43."..ii,
        [[
            SELECT a FROM (SELECT * FROM t1 ORDER BY a LIMIT 3)
            UNION SELECT a+10 FROM (SELECT a FROM t1 ORDER BY a LIMIT 4)
            UNION SELECT a+20 FROM (SELECT a FROM t1 ORDER BY a LIMIT 2)
            ORDER BY 1
            LIMIT ]]..ii, lrange({ 1, 2, 3, 11, 12, 13, 14, 21, 22 }, 0 ,  ii-1 ))

end
for ii = 1, 7, 1 do
    jj = (7 - ii)
    test:do_execsql_test(
        "tkt-38cb5df375.51."..ii,
        [[
            SELECT a FROM (SELECT * FROM t1 ORDER BY a)
            EXCEPT SELECT a FROM (SELECT a FROM t1 ORDER BY a LIMIT ]]..ii..[[)
            ORDER BY a DESC
            LIMIT ]]..jj, lrange({ 8, 7, 6, 5, 4, 3, 2, 1 }, 0 ,  jj-1 ))

end
test:finish_test()

