#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(19)

--!./tcltestrunner.lua
-- 2014-10-11
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
---------------------------------------------------------------------------
--
-- Test that ticket [ba7cbfaedc] has been fixed.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
local testprefix = "tkt-ba7cbfaedc"
test:do_execsql_test(
    1,
    [[
        CREATE TABLE t1 (id  INT primary key, x INT , y TEXT);
        INSERT INTO t1 VALUES (1, 3, 'a');
        INSERT INTO t1 VALUES (2, 1, 'a'); 
        INSERT INTO t1 VALUES (3, 2, 'b');
        INSERT INTO t1 VALUES (4, 2, 'a');
        INSERT INTO t1 VALUES (5, 3, 'b');
        INSERT INTO t1 VALUES (6, 1, 'b'); 
    ]])

test:do_execsql_test(
    1.1,
    [[
        CREATE INDEX i1 ON t1(x, y);
    ]])

local idxs = {
    "CREATE INDEX i1 ON t1(x, y)",
    "CREATE INDEX i1 ON t1(x DESC, y)",
    "CREATE INDEX i1 ON t1(x, y DESC)",
    "CREATE INDEX i1 ON t1(x DESC, y DESC)"
}

for n, idx in ipairs(idxs) do
    test:catchsql " DROP INDEX i1 ON t1"
    test:execsql(idx)
    local queries = {
        {"GROUP BY x, y ORDER BY x, y", {1, 'a', 1, "b", 2, "a", 2, "b", 3, "a", 3, "b"}},
        {"GROUP BY x, y ORDER BY x DESC, y", {3, "a", 3, "b", 2, "a", 2, "b", 1, "a", 1, "b"}},
        {"GROUP BY x, y ORDER BY x, y DESC", {1, "b", 1, "a", 2, "b", 2, "a", 3, "b", 3, "a"}},
        {"GROUP BY x, y ORDER BY x DESC, y DESC", {3, "b", 3, "a", 2, "b", 2, "a", 1, "b", 1, "a"}},
    }
    for tn, val in ipairs(queries) do
        local q = val[1]
        local res = val[2]
        test:do_execsql_test(
            string.format("1.%s.%s", n, tn),
            "SELECT x,y FROM t1 "..q.."", res)

    end
end
test:do_test(
    2.0,
    function ()
        test:execsql([[
            drop table if exists t1;
            create table t1(id int primary key);
            insert into t1(id) values(1),(2),(3),(4),(5);
            create index t1_idx_id on t1(id asc);
        ]])
        local res = {test:execsql("select * from t1 group by id order by id;")}
        table.insert(res, test:execsql("select * from t1 group by id order by id asc;"))
        table.insert(res, test:execsql("select * from t1 group by id order by id desc;"))
        return res
    end
     , {
        -- <2.0>
        {1, 2, 3, 4, 5},
        {1, 2, 3, 4, 5},
        {5, 4, 3, 2, 1},
        -- </2.0>
    })

test:finish_test()

