#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(57)

--!./tcltestrunner.lua
-- 2011 July 1
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
-- focus of this script is the DISTINCT modifier.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]

local testprefix = "distinct"
local function is_distinct_noop(sql)
    local sql1 = sql
    local sql2 = string.gsub(sql, "DISTINCT", "")
    local program1 = {  }
    local program2 = {  }
    local r = box.sql.execute("EXPLAIN "..sql1)
    for _, val in ipairs(r) do
        local opcode = val[2]
        if opcode ~= "Noop" then
            table.insert(program1, opcode)
        end
    end
    r = box.sql.execute("EXPLAIN "..sql2)
    for _, val in ipairs(r) do
        local opcode = val[2]
        if opcode ~= "Noop" then
            table.insert(program2, opcode)
        end
    end
    return test.is_deeply_regex(program1, program2)
end

local function do_distinct_noop_test(tn, sql)
    test:do_test(
        tn,
        function()
            return is_distinct_noop(sql)
        end,true)
end

local function do_distinct_not_noop_test(tn, sql)
    test:do_test(
        tn,
        function()
            return is_distinct_noop(sql)
        end,false)
end

local function do_temptables_test(tn, sql, temptables)
    test:do_test(
        tn,
        function()
            local ret = {}
            local r = box.sql.execute("EXPLAIN "..sql)
            for _, val in ipairs(r) do
                local opcode = val[2]
                local p5 = val[7]
                if opcode == "OpenEphemeral" or opcode == "SorterOpen" then
                    if p5 ~= "08" and p5 ~= "00" then
                        error()--p5 = $p5)
                    end
                    if p5 == "08" then
                            table.insert(ret, "hash")
                    else
                            table.insert(ret, "btree")
                end
                end
            end
            return ret
        end,
        temptables)
end

---------------------------------------------------------------------------
-- The following tests - distinct-1.* - check that the planner correctly 
-- detects cases where a UNIQUE index means that a DISTINCT clause is 
-- redundant. Currently the planner only detects such cases when there
-- is a single table in the FROM clause.
--
test:do_execsql_test(
    1.0,
    [[
        CREATE TABLE t1(id INTEGER PRIMARY KEY, a, b, c, d);
        CREATE UNIQUE INDEX i2 ON t1(d COLLATE nocase);

        CREATE TABLE t2(x INTEGER PRIMARY KEY, y);

        CREATE TABLE t3(c1 PRIMARY KEY NOT NULL, c2 NOT NULL);
        CREATE INDEX i3 ON t3(c2);

        CREATE TABLE t4(id INTEGER PRIMARY KEY, a, b NOT NULL, c NOT NULL, d NOT NULL);
        CREATE UNIQUE INDEX t4i1 ON t4(b, c);
        CREATE UNIQUE INDEX t4i2 ON t4(d COLLATE nocase);
    ]])
local data = {
    {"1.1", 0, "SELECT DISTINCT b, c FROM t1"},
    {"1.2", 1, "SELECT DISTINCT b, c FROM t4"},
    {"2.1", 0, "SELECT DISTINCT c FROM t1 WHERE b = ?"},
    {"2.2", 1, "SELECT DISTINCT c FROM t4 WHERE b = ?"},
    {"5 ", 1, "SELECT DISTINCT x FROM t2"},
    {"6 ", 1, "SELECT DISTINCT * FROM t2"},
    {"7 ", 1, "SELECT DISTINCT * FROM (SELECT * FROM t2)"},
    {"8.1", 1, "SELECT DISTINCT * FROM t1"},
    {"8.2", 1, "SELECT DISTINCT * FROM t4"},
    {"8 ", 0, "SELECT DISTINCT a, b FROM t1"},
    {"9 ", 0, "SELECT DISTINCT c FROM t1 WHERE b IN (1,2)"},
    {"10 ", 0, "SELECT DISTINCT c FROM t1"},
    {"11 ", 0, "SELECT DISTINCT b FROM t1"},
    {"12.1", 0, "SELECT DISTINCT a, d FROM t1"},
    {"12.2", 0, "SELECT DISTINCT a, d FROM t4"},
    {"13.1", 0, "SELECT DISTINCT a, b, c COLLATE nocase FROM t1"},
    {"13.2", 0, "SELECT DISTINCT a, b, c COLLATE nocase FROM t4"},
    {"14.1", 0, "SELECT DISTINCT a, d COLLATE nocase FROM t1"},
    {"14.2", 1, "SELECT DISTINCT a, d COLLATE nocase FROM t4"},
    {"15 ", 0, "SELECT DISTINCT a, d COLLATE binary FROM t1"},
    {"16.1", 0, "SELECT DISTINCT a, b, c COLLATE binary FROM t1"},
    {"16.2", 1, "SELECT DISTINCT a, b, c COLLATE binary FROM t4"},
    {"17",  0,   --{ \/* Technically, it would be possible to detect that DISTINCT\n            ** is a no-op in cases like the following. But SQLite does not\n            ** do so. *\/\n
    "SELECT DISTINCT t1.id FROM t1, t2 WHERE t1.id=t2.x" },
    {"18 ", 1, "SELECT DISTINCT c1, c2 FROM t3"},
    {"19 ", 1, "SELECT DISTINCT c1 FROM t3"},
    {"20 ", 1, "SELECT DISTINCT * FROM t3"},
    {"21 ", 0, "SELECT DISTINCT c2 FROM t3"},
    {"22 ", 0, "SELECT DISTINCT * FROM (SELECT 1, 2, 3 UNION SELECT 4, 5, 6)"},
    {"23 ", 1, "SELECT DISTINCT rowid FROM (SELECT 1, 2, 3 UNION SELECT 4, 5, 6)"},

}

for _, val in ipairs(data) do
    local tn = val[1]
    local noop = val[2]
    local sql = val[3]
    if noop == 1 then
        do_distinct_noop_test("1."..tn, sql)
    else
        do_distinct_not_noop_test("1."..tn, sql)
    end
end
---------------------------------------------------------------------------
-- The following tests - distinct-2.* - test cases where an index is
-- used to deliver results in order of the DISTINCT expressions. 
--
--X(143, "X!cmd", [=[["drop_all_tables"]]=])
test:execsql([[
    DROP TABLE t1;
]])
test:do_execsql_test(
    2.0,
    [[
        CREATE TABLE t1(id INTEGER PRIMARY KEY, a, b, c);
        CREATE INDEX i1 ON t1(a, b);
        CREATE INDEX i2 ON t1(b COLLATE nocase, c COLLATE nocase);

        INSERT INTO t1 VALUES(1, 'a', 'b', 'c');
        INSERT INTO t1 VALUES(2, 'A', 'B', 'C');
        INSERT INTO t1 VALUES(3, 'a', 'b', 'c');
        INSERT INTO t1 VALUES(4, 'A', 'B', 'C');
    ]])

data = {
    {"a, b FROM t1", {}, {"A", "B", "a", "b"}},
    {"b, a FROM t1", {}, {"B", "A", "b", "a"}},
    {"a, b, c FROM t1", {"hash"}, {"A", "B", "C", "a", "b", "c"}},
    {"a, b, c FROM t1 ORDER BY a, b, c", {"btree"}, {"A", "B", "C", "a", "b", "c"}},
    {"b FROM t1 WHERE a = 'a'", {}, {"b"}},
    {"b FROM t1 ORDER BY +b COLLATE binary", {"btree", "hash"}, {"B", "b"}},
    {"a FROM t1", {}, {"A", "a"}},
    {"b COLLATE nocase FROM t1", {}, {"B"}},
    {"b COLLATE nocase FROM t1 ORDER BY b COLLATE nocase", {}, {"B"}},
}
for tn, val in ipairs(data) do
    local sql = val[1]
    local temptables = val[2]
    local res = val[3]
    test:do_execsql_test(
        "2."..tn..".1",
        "SELECT DISTINCT "..sql.."",
        res)

    do_temptables_test("2."..tn..".2", "SELECT DISTINCT "..sql.."", temptables)
end
test:do_execsql_test(
    "2.A",
    [[
        SELECT (SELECT DISTINCT o.a FROM t1 AS i) FROM t1 AS o ORDER BY id;
    ]], {
        -- <2.A>
        "a", "A", "a", "A"
        -- </2.A>
    })

-- do_test 3.0 {
--   db eval {
--     CREATE TABLE t3(a INTEGER, b INTEGER, c, UNIQUE(a,b));
--     INSERT INTO t3 VALUES
--         (null, null, 1),
--         (null, null, 2),
--         (null, 3, 4),
--         (null, 3, 5),
--         (6, null, 7),
--         (6, null, 8);
--     SELECT DISTINCT a, b FROM t3 ORDER BY +a, +b;
--   }
-- } {{} {} {} 3 6 {}}
-- do_test 3.1 {
--   regexp {OpenEphemeral} [db eval {
--     EXPLAIN SELECT DISTINCT a, b FROM t3 ORDER BY +a, +b;
--   }]
-- } {0}
-- MUST_WORK_TEST
if (1 > 0) then
    ---------------------------------------------------------------------------
    -- Ticket  [fccbde530a6583bf2748400919f1603d5425995c] (2014-01-08)
    -- The logic that computes DISTINCT sometimes thinks that a zeroblob()
    -- and a blob of all zeros are different when they should be the same. 
    --
    test:do_execsql_test(
        4.1,
        [[
            DROP TABLE IF EXISTS t1;
            DROP TABLE IF EXISTS t2;
            CREATE TABLE t1(id primary key, a INTEGER);
            INSERT INTO t1 VALUES(1,3);
            INSERT INTO t1 VALUES(2,2);
            INSERT INTO t1 VALUES(3,1);
            INSERT INTO t1 VALUES(4,2);
            INSERT INTO t1 VALUES(5,3);
            INSERT INTO t1 VALUES(6,1);
            CREATE TABLE t2(x primary key);
            INSERT INTO t2
              SELECT DISTINCT
                CASE a WHEN 1 THEN x'0000000000'
                       WHEN 2 THEN zeroblob(5)
                       ELSE 'xyzzy' END
                FROM t1;
            SELECT quote(x) FROM t2 ORDER BY 1;
        ]], {
            -- <4.1>
            "'xyzzy'", "X'0000000000'"
            -- </4.1>
        })

    ------------------------------------------------------------------------------
    -- Ticket [c5ea805691bfc4204b1cb9e9aa0103bd48bc7d34] (2014-12-04)
    -- Make sure that DISTINCT works together with ORDER BY and descending
    -- indexes.
    --
    test:do_execsql_test(
        5.1,
        [[
            DROP TABLE IF EXISTS t1;
            CREATE TABLE t1(id primary key,x);
            INSERT INTO t1(id,x) VALUES(1,3),(2,1),(3,5),
                                    (4,2),(5,6),(6,4),
                                    (7,5),(8,1),(9,3);
            CREATE INDEX t1x ON t1(x DESC);
            SELECT DISTINCT x FROM t1 ORDER BY x ASC;
        ]], {
            -- <5.1>
            1, 2, 3, 4, 5, 6
            -- </5.1>
        })

    test:do_execsql_test(
        5.2,
        [[
            SELECT DISTINCT x FROM t1 ORDER BY x DESC;
        ]], {
            -- <5.2>
            6, 5, 4, 3, 2, 1
            -- </5.2>
        })

    test:do_execsql_test(
        5.3,
        [[
            SELECT DISTINCT x FROM t1 ORDER BY x;
        ]], {
            -- <5.3>
            1, 2, 3, 4, 5, 6
            -- </5.3>
        })

    test:do_execsql_test(
        5.4,
        [[
            DROP INDEX t1x;
            CREATE INDEX t1x ON t1(x ASC);
            SELECT DISTINCT x FROM t1 ORDER BY x ASC;
        ]], {
            -- <5.4>
            1, 2, 3, 4, 5, 6
            -- </5.4>
        })

    test:do_execsql_test(
        5.5,
        [[
            SELECT DISTINCT x FROM t1 ORDER BY x DESC;
        ]], {
            -- <5.5>
            6, 5, 4, 3, 2, 1
            -- </5.5>
        })

    test:do_execsql_test(
        5.6,
        [[
            SELECT DISTINCT x FROM t1 ORDER BY x;
        ]], {
            -- <5.6>
            1, 2, 3, 4, 5, 6
            -- </5.6>
        })

end


test:finish_test()
