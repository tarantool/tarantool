#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(88)

local json = require("json")

--!./tcltestrunner.lua
-- 2011 January 19
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
-- This file implements tests for sql library.  The focus of the tests
-- in this file is the use of the sql_stat4 histogram data on tables
-- with many repeated values and only a few distinct values.
--

local function eqp(sql) -- luacheck: no unused
    return test:execsql("EXPLAIN QUERY PLAN"..sql)
end

-- Legacy from the original code. Must be replaced with analogue
-- function.
local X = nil

local function alpha(blob) -- luacheck: no unused
    local ret = ""
    for _, c in ipairs(X(37, "X!cmd", [=[["split",["blob"],""]]=])) do
        if X(39, "X!cmd", [=[["string","is","alpha",["c"]]]=])
 then
            ret = ret .. c
        end
    end
    return ret
end

-- db("func", "alpha", "alpha")
-- db("func", "lindex", "lindex")
-- ["unset","-nocomplain","i","t","u","v","w","x","y","z"]

test:do_test(
    "analyze5-0.0",
    function()
        box.internal.sql_create_function(
            "msgpack_decode",
            "BLOB",
            function(txt)
                -- MsgPack, must contain single-element array w/ string
                return require('msgpack').decode(txt)[1]
            end)

        return 1
    end,
    1)

-- MUST_WORK_TEST
test:do_test(
    "analyze5-1.0",
    function()
        -- Tarantool: waiting for #2130
        -- test:execsql("CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, t INT ,u INT ,v TEXT COLLATE nocase,w INT ,x INT ,y INT ,z INT )")
        test:execsql("CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT, t TEXT ,u TEXT ,v TEXT ,w TEXT ,x TEXT ,y TEXT ,z NUMBER)")
        for i=0,999 do -- _ in X(0, "X!for", [=[["set i 0","$i < 1000","incr i"]]=]) do
            local y
            if  ((i >= 25) and (i <= 50)) then
                y = 1
            else
                y = 0
            end

            local z = 0
            if i >= 400 then
                z = 1
            end
            if  i >= 700 then
                z = z + 1
            end
            if  i >= 875 then
                z = z + 1
            end

            local x, w, t, u
            x = z
            w = z
            t = (z + 0.5)
            if z == 0 then
                u = "alpha"
                x = 'NULL'
            elseif z == 1 then
                u = "bravo"
            elseif z == 2 then
                u = "charlie"
            elseif z == 3 then
                u = "delta"
                w = 'NULL'
            end
            -- Tarantool: commented until #2121 is resolved
            -- if X(65, "X!cmd", [=[["expr","$i%2"]]=]) then
            --    v = u
            -- end
            local v = 'NULL'
            test:execsql("INSERT INTO t1 (t,u,v,w,x,y,z) VALUES('"..t.."','"..u.."','"..v.."','"..w.."','"..x.."','"..y.."','"..z.."')")
        end
        test:execsql([[
            CREATE INDEX t1t ON t1(t);  -- 0.5, 1.5, 2.5, and 3.5
            CREATE INDEX t1u ON t1(u);  -- text
            CREATE INDEX t1v ON t1(v);  -- mixed case text
            CREATE INDEX t1w ON t1(w);  -- integers 0, 1, 2 and a few NULLs
            CREATE INDEX t1x ON t1(x);  -- integers 1, 2, 3 and many NULLs
            CREATE INDEX t1y ON t1(y);  -- integers 0 and very few 1s
            CREATE INDEX t1z ON t1(z);  -- integers 0, 1, 2, and 3
            ANALYZE;
        ]])

        -- DISTINCT idx, sample -- lindex(test_decode(sample),0)
        -- WHERE idx='t1u' ORDER BY nlt;
        return test:execsql([[ SELECT DISTINCT msgpack_decode("sample")
                                 FROM "_sql_stat4"
                                 WHERE "idx"='T1U'
                                 ORDER BY "nlt"]])
    end, {
        -- <analyze5-1.0>
        "alpha", "bravo", "charlie", "delta"
        -- </analyze5-1.0>
    })

-- Waiting for #2121...
-- test:do_test(
--     "analyze5-1.1",
--     function()
--         return test:execsql([[
--             SELECT DISTINCT lower(lindex(test_decode(sample), 0))
--               FROM _sql_stat4 WHERE idx='t1v' ORDER BY 1
--         ]])


--     end, {
--         -- <analyze5-1.1>
--         "alpha", "bravo", "charlie", "delta"
--         -- </analyze5-1.1>
--     })

test:do_test(
    "analyze5-1.2",
    function()
        return test:execsql([[SELECT "idx", count(*) FROM "_sql_stat4" GROUP BY 1 ORDER BY 1]])
    end, {
        -- <analyze5-1.2>
        "T1",24,"T1T",4,"T1U",4,"T1V",1,"T1W",4,"T1X",4,"T1Y",2,"T1Z",4
        -- </analyze5-1.2>
    })

-- Verify that range queries generate the correct row count estimates
--
-- Legacy from the original code. Must be replaced with analogue
-- functions.
local t1x = nil
local t1y = nil
local t1z = nil
for i, v in pairs({
{'z>=0 AND z<=0',      t1z,  400},
{'z>=1 AND z<=1',      t1z,  300},
{'z>=2 AND z<=2',      t1z,  175},
{'z>=3 AND z<=3',      t1z,  125},
{'z>=4 AND z<=4',      t1z,    1},
{'z>=-1 AND z<=-1',    t1z,    1},
{'z>1 AND z<3',        t1z,  175},
{'z>0 AND z<100',      t1z,  600},
{'z>=1 AND z<100',     t1z,  600},
{'z>1 AND z<100',      t1z,  300},
{'z>=2 AND z<100',     t1z,  300},
{'z>2 AND z<100',      t1z,  125},
{'z>=3 AND z<100',     t1z,  125},
{'z>3 AND z<100',      t1z,    1},
{'z>=4 AND z<100',     t1z,    1},
{'z>=-100 AND z<=-1',  t1z,    1},
{'z>=-100 AND z<=0',   t1z,  400},
{'z>=-100 AND z<0',    t1z,    1},
{'z>=-100 AND z<=1',   t1z,  700},
{'z>=-100 AND z<2',    t1z,  700},
{'z>=-100 AND z<=2',   t1z,  875},
{'z>=-100 AND z<3',    t1z,  875},

{'z>=0.0 AND z<=0.0',  t1z,  400},
{'z>=1.0 AND z<=1.0',  t1z,  300},
{'z>=2.0 AND z<=2.0',  t1z,  175},
{'z>=3.0 AND z<=3.0',  t1z,  125},
{'z>=4.0 AND z<=4.0',  t1z,    1},
{'z>=-1.0 AND z<=-1.0',t1z,    1},
{'z>1.5 AND z<3.0',    t1z,  174},
{'z>0.5 AND z<100',    t1z,  599},
{'z>=1.0 AND z<100',   t1z,  600},
{'z>1.5 AND z<100',    t1z,  299},
{'z>=2.0 AND z<100',   t1z,  300},
{'z>2.1 AND z<100',    t1z,  124},
{'z>=3.0 AND z<100',   t1z,  125},
{'z>3.2 AND z<100',    t1z,    1},
{'z>=4.0 AND z<100',   t1z,    1},
{'z>=-100 AND z<=-1.0',t1z,    1},
{'z>=-100 AND z<=0.0', t1z,  400},
{'z>=-100 AND z<0.0',  t1z,    1},
{'z>=-100 AND z<=1.0', t1z,  700},
{'z>=-100 AND z<2.0',  t1z,  700},
{'z>=-100 AND z<=2.0', t1z,  875},
{'z>=-100 AND z<3.0',  t1z,  875},

{'z=-1',               t1z,    1},
{'z=0',                t1z,  400},
{'z=1',                t1z,  300},
{'z=2',                t1z,  175},
{'z=3',                t1z,  125},
{'z=4',                t1z,    1},
{'z=-10.0',            t1z,    1},
{'z=0.0',              t1z,  400},
{'z=1.0',              t1z,  300},
{'z=2.0',              t1z,  175},
{'z=3.0',              t1z,  125},
{'z=4.0',              t1z,    1},
{'z=1.5',              t1z,    1},
{'z=2.5',              t1z,    1},

{'z IN (-1)',          t1z,    1},
{'z IN (0)',           t1z,  400},
{'z IN (1)',           t1z,  300},
{'z IN (2)',           t1z,  175},
{'z IN (3)',           t1z,  125},
{'z IN (4)',           t1z,    1},
{'z IN (0.5)',         t1z,    1},
{'z IN (0,1)',         t1z,  700},
{'z IN (0,1,2)',       t1z,  875},
{'z IN (0,1,2,3)',     nil,  100},
{'z IN (0,1,2,3,4,5)', nil,  100},
{'z IN (1,2)',         t1z,  475},
{'z IN (2,3)',         t1z,  300},
{'z=3 OR z=2',         t1z,  300},
{'z IN (-1,3)',        t1z,  126},
{'z=-1 OR z=3',        t1z,  126},

{'y=0',                t1y,  974},
{'y=1',                t1y,   26},
{'y=0.1',              t1y,    1},

{'x IS NULL',          t1x,  400},
                 }) do
    -- Verify that the expected index is used with the expected row count
    -- No longer valid due to an EXPLAIN QUERY PLAN output format change
    -- do_test analyze5-1.${testid}a {
    --   set x [lindex [eqp "SELECT * FROM t1 WHERE $where"] 3]
    --   set idx {}
    --   regexp {INDEX (t1.) } $x all idx
    --   regexp {~([0-9]+) rows} $x all nrow
    --   list $idx $nrow
    -- } [list $index $rows]
    -- Verify that the same result is achieved regardless of whether or not
    -- the index is used
    test:do_test(
        "analyze5-1."..i.."b",
        function()
            local w2, a1, a2, res
            w2 = v[1]:gsub('y', '+y'):gsub('z', '+z')
            a1 = test:execsql("SELECT id FROM t1 NOT INDEXED WHERE "..w2.." ORDER BY +id")
            a2 = test:execsql("SELECT id FROM t1 WHERE "..v[1].." ORDER BY +id")
            if (test.is_deeply_regex(a1, a2))
            then
                res = "ok"
            else
                res = string.format("a1=%s a2=%s", json.encode(a1), json.encode(a2))
            end
            return res
        end,
        "ok")
end
-- Increase the number of NULLs in column x
--
test:execsql([[
    UPDATE t1 SET x=NULL;
    UPDATE t1 SET x=id
     WHERE id IN (SELECT id FROM t1 ORDER BY random() LIMIT 5);
    ANALYZE;
]])
-- Verify that range queries generate the correct row count estimates
--
for i, v in pairs({
[[x IS NULL AND u='charlie']],
[[x=1 AND u='charlie']],
[[x IS NULL]],
[[x=1]],
[[x IS NOT NULL]],
[[+x IS NOT NULL]],
[[upper(x) IS NOT NULL]],
                 }) do
    -- Verify that the expected index is used with the expected row count
    -- No longer valid due to an EXPLAIN QUERY PLAN format change
    -- do_test analyze5-1.${testid}a {
    --   set x [lindex [eqp "SELECT * FROM t1 WHERE $where"] 3]
    --   set idx {}
    --   regexp {INDEX (t1.) } $x all idx
    --   regexp {~([0-9]+) rows} $x all nrow
    --   list $idx $nrow
    -- } [list $index $rows]
    -- Verify that the same result is achieved regardless of whether or not
    -- the index is used
    test:do_test(
        "analyze5-1."..i.."b",
        function()
            local a1, a2, res
            a1 = test:execsql("SELECT id FROM t1 NOT INDEXED WHERE "..v.." ORDER BY +id")
            a2 = test:execsql("SELECT id FROM t1 WHERE "..v.." ORDER BY +id")
            if (test.is_deeply_regex(a1, a1))
            then
                res = "ok"
            else
                res = string.format("a1=%s a2=%s", json.encode(a1), json.encode(a2))
            end
            return res
        end,
        "ok")

end

test:finish_test()
