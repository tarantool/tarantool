#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(83)

--!./tcltestrunner.lua
-- 2006 January 31
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
-- focus of this file is testing the join reordering optimization
-- in cases that include a LEFT JOIN.
--
-- $Id: where3.test,v 1.4 2008/04/17 19:14:02 drh Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- The following is from ticket #1652.
--
-- A comma join then a left outer join:  A,B left join C.
-- Arrange indices so that the B table is chosen to go first.
-- Also put an index on C, but make sure that A is chosen before C.
--
test:do_execsql_test(
    "where3-1.1",
    [[
        CREATE TABLE t1(a  INT primary key, b TEXT);
        CREATE TABLE t2(p  INT primary key, q INT );
        CREATE TABLE t3(x  INT primary key, y TEXT);

        INSERT INTO t1 VALUES(111,'one');
        INSERT INTO t1 VALUES(222,'two');
        INSERT INTO t1 VALUES(333,'three');

        INSERT INTO t2 VALUES(1,111);
        INSERT INTO t2 VALUES(2,222);
        INSERT INTO t2 VALUES(4,444);
        CREATE INDEX t2i1 ON t2(p);

        INSERT INTO t3 VALUES(999,'nine');
        CREATE INDEX t3i1 ON t3(x);

        SELECT * FROM t1, t2 LEFT JOIN t3 ON q=x WHERE p=2 AND a=q;
    ]], {
        -- <where3-1.1>
        222, "two", 2, 222, "", ""
        -- </where3-1.1>
    })

test:do_test(
    "where3-1.1.1",
    function() return test:explain_no_trace("SELECT * FROM t1, t2 LEFT JOIN t3 ON q=x WHERE p=2 AND a=q") end,
    test:explain_no_trace("SELECT * FROM t1, t2 LEFT JOIN t3 ON x=q WHERE p=2 AND a=q"))

-- Ticket #1830
--
-- This is similar to the above but with the LEFT JOIN on the
-- other side.
--
test:do_execsql_test(
    "where3-1.2",
    [[
        CREATE TABLE test1(parent1key  INT primary key, child1key TEXT, Child2key TEXT , child3key INT );
        CREATE TABLE child1 ( child1key  TEXT primary key, value  TEXT );
        CREATE TABLE child2 ( child2key  TEXT primary key, value  TEXT );

        INSERT INTO test1(parent1key,child1key,child2key)
           VALUES ( 1, 'C1.1', 'C2.1' );
        INSERT INTO child1 ( child1key, value ) VALUES ( 'C1.1', 'Value for C1.1' );
        INSERT INTO child2 ( child2key, value ) VALUES ( 'C2.1', 'Value for C2.1' );

        INSERT INTO test1 ( parent1key, child1key, child2key )
           VALUES ( 2, 'C1.2', 'C2.2' );
        INSERT INTO child2 ( child2key, value ) VALUES ( 'C2.2', 'Value for C2.2' );

        INSERT INTO test1 ( parent1key, child1key, child2key )
           VALUES ( 3, 'C1.3', 'C2.3' );
        INSERT INTO child1 ( child1key, value ) VALUES ( 'C1.3', 'Value for C1.3' );
        INSERT INTO child2 ( child2key, value ) VALUES ( 'C2.3', 'Value for C2.3' );

        SELECT test1.parent1key, child1.value, child2.value
        FROM test1
        LEFT OUTER JOIN child1 ON child1.child1key = test1.child1key
        INNER JOIN child2 ON child2.child2key = test1.child2key;
    ]], {
        -- <where3-1.2>
        1, "Value for C1.1", "Value for C2.1", 2, "", "Value for C2.2", 3, "Value for C1.3", "Value for C2.3"
        -- </where3-1.2>
    })

test:do_test(
    "where3-1.2.1",
    function()
        return test:explain_no_trace([[SELECT test1.parent1key, child1.value, child2.value
                                       FROM test1
                                       LEFT OUTER JOIN child1 ON child1.child1key = test1.child1key
                                       INNER JOIN child2 ON child2.child2key = test1.child2key;]])
     end,
        -- <where3-1.2.1>
        test:explain_no_trace([[
            SELECT test1.parent1key, child1.value, child2.value
            FROM test1
            LEFT OUTER JOIN child1 ON test1.child1key = child1.child1key
            INNER JOIN child2 ON child2.child2key = test1.child2key;
        ]])
        -- </where3-1.2.1>
    )



-- This procedure executes the SQL.  Then it appends 
-- the names of the table and index used
--
local function queryplan(sql)
    -- sql_sort_count = 0
    local data, eqp
    data = test:execsql(sql)
    eqp = test:execsql("EXPLAIN QUERY PLAN "..sql)
    for i,v in ipairs(eqp) do
        if i % 4 == 0 then
            local as, tab, idx = string.match(v, "TABLE (%w+AS )(%w+) USING.*INDEX (%w+)")
            if as == nil then
                tab, idx = string.match(v, "TABLE (%w+) USING.*INDEX (%w+)")
            end

            if tab ~= nil then
                table.insert(data, tab)
                table.insert(data, idx)
            else
                as, tab = string.match(v, "TABLE (%w+ AS ) (%w+)")
                if as == nil  then
                    tab = string.match(v, "TABLE (%w+)")
                end

                if tab ~= nil then
                    table.insert(data, tab)
                    table.insert(data, '*')
                end
            end
        end
    end
    -- puts eqp=$eqp
 --    for _ in X(0, "X!foreach", [=[["a b c x",["eqp"]]]=]) do
 --        if X(119, "X!cmd", [=[["regexp"," TABLE (\\w+ AS )?(\\w+) USING.* INDEX (\\w+)\\y",["x"],"all","as","tab","idx"]]=])
 -- then
 --            table.insert(data,tab, idx) or data
 --        elseif X(119, "X!cmd", [=[["expr","[regexp { TABLE (\\w+ AS )?(\\w+)\\y} $x all as tab]"]]=])
 -- then
 --            table.insert(data,tab, "*") or data
 --        end
 --    end
    return data
end

-- If you have a from clause of the form:   A B C left join D
-- then make sure the query optimizer is able to reorder the 
-- A B C part anyway it wants. 
--
-- Following the fix to ticket #1652, there was a time when
-- the C table would not reorder.  So the following reorderings
-- were possible:
--
--            A B C left join D
--            B A C left join D
--
-- But these reorders were not allowed
--
--            C A B left join D
--            A C B left join D
--            C B A left join D
--            B C A left join D
--
-- The following tests are here to verify that the latter four
-- reorderings are allowed again.
--
test:do_test(
    "where3-2.1",
    function()
        test:execsql [[
            CREATE TABLE tA(apk integer primary key, ax INT );
            CREATE TABLE tB(bpk integer primary key, bx INT );
            CREATE TABLE tC(cpk integer primary key, cx INT );
            CREATE TABLE tD(dpk integer primary key, dx INT );
        ]]
        return queryplan([[
    SELECT * FROM tA, tB, tC LEFT JOIN tD ON dpk=cx
     WHERE cpk=bx AND bpk=ax
  ]])
    end, {
        -- <where3-2.1>
        "TA", "*", "TB", "*", "TC", "*", "TD", "*"
        -- </where3-2.1>
    })

test:do_test(
    "where3-2.1.1",
    function()
        return queryplan([[
    SELECT * FROM tA, tB, tC LEFT JOIN tD ON cx=dpk
     WHERE cpk=bx AND bpk=ax
  ]])
    end, {
        -- <where3-2.1.1>
        "TA", "*", "TB", "*", "TC", "*", "TD", "*"
        -- </where3-2.1.1>
    })

test:do_test(
    "where3-2.1.2",
    function()
        return queryplan([[
    SELECT * FROM tA, tB, tC LEFT JOIN tD ON cx=dpk
     WHERE bx=cpk AND bpk=ax
  ]])
    end, {
        -- <where3-2.1.2>
        "TA", "*", "TB", "*", "TC", "*", "TD", "*"
        -- </where3-2.1.2>
    })

test:do_test(
    "where3-2.1.3",
    function()
        return queryplan([[
    SELECT * FROM tA, tB, tC LEFT JOIN tD ON cx=dpk
     WHERE bx=cpk AND ax=bpk
  ]])
    end, {
        -- <where3-2.1.3>
        "TA", "*", "TB", "*", "TC", "*", "TD", "*"
        -- </where3-2.1.3>
    })

test:do_test(
    "where3-2.1.4",
    function()
        return queryplan([[
    SELECT * FROM tA, tB, tC LEFT JOIN tD ON dpk=cx
     WHERE bx=cpk AND ax=bpk
  ]])
    end, {
        -- <where3-2.1.4>
        "TA", "*", "TB", "*", "TC", "*", "TD", "*"
        -- </where3-2.1.4>
    })

test:do_test(
    "where3-2.1.5",
    function()
        return queryplan([[
    SELECT * FROM tA, tB, tC LEFT JOIN tD ON dpk=cx
     WHERE cpk=bx AND ax=bpk
  ]])
    end, {
        -- <where3-2.1.5>
        "TA", "*", "TB", "*", "TC", "*", "TD", "*"
        -- </where3-2.1.5>
    })

test:do_test(
    "where3-2.2",
    function()
        return queryplan([[
    SELECT * FROM tA, tB, tC LEFT JOIN tD ON dpk=cx
     WHERE cpk=bx AND apk=bx
  ]])
    end, {
        -- <where3-2.2>
        "TB", "*", "TA", "*", "TC", "*", "TD", "*"
        -- </where3-2.2>
    })

test:do_test(
    "where3-2.3",
    function()
        return queryplan([[
    SELECT * FROM tA, tB, tC LEFT JOIN tD ON dpk=cx
     WHERE cpk=bx AND apk=bx
  ]])
    end, {
        -- <where3-2.3>
        "TB", "*", "TA", "*", "TC", "*", "TD", "*"
        -- </where3-2.3>
    })

test:do_test(
    "where3-2.4",
    function()
        return queryplan([[
    SELECT * FROM tA, tB, tC LEFT JOIN tD ON dpk=cx
     WHERE apk=cx AND bpk=ax
  ]])
    end, {
        -- <where3-2.4>
        "TC", "*", "TA", "*", "TB", "*", "TD", "*"
        -- </where3-2.4>
    })

test:do_test(
    "where3-2.5",
    function()
        return queryplan([[
    SELECT * FROM tA, tB, tC LEFT JOIN tD ON dpk=cx
     WHERE cpk=ax AND bpk=cx
  ]])
    end, {
        -- <where3-2.5>
        "TA", "*", "TC", "*", "TB", "*", "TD", "*"
        -- </where3-2.5>
    })

test:do_test(
    "where3-2.6",
    function()
        return queryplan([[
    SELECT * FROM tA, tB, tC LEFT JOIN tD ON dpk=cx
     WHERE bpk=cx AND apk=bx
  ]])
    end, {
        -- <where3-2.6>
        "TC", "*", "TB", "*", "TA", "*", "TD", "*"
        -- </where3-2.6>
    })

test:do_test(
    "where3-2.7",
    function()
        return queryplan([[
    SELECT * FROM tA, tB, tC LEFT JOIN tD ON dpk=cx
     WHERE cpk=bx AND apk=cx
  ]])
    end, {
        -- <where3-2.7>
        "TB", "*", "TC", "*", "TA", "*", "TD", "*"
        -- </where3-2.7>
    })

-- Ticket [13f033c865f878953]
-- If the outer loop must be a full table scan, do not let ANALYZE trick
-- the planner into use a table for the outer loop that might be indexable
-- if held until an inner loop.
-- 
test:do_execsql_test(
    "where3-3.0",
    [[
        CREATE TABLE t301(a INTEGER PRIMARY KEY,b INT ,c INT );
        CREATE INDEX t301c ON t301(c);
        INSERT INTO t301 VALUES(1,2,3);
        INSERT INTO t301 VALUES(2,2,3);
        CREATE TABLE t302(x  INT primary key, y INT );
        INSERT INTO t302 VALUES(4,5);
        SELECT * FROM t302, t301 WHERE t302.x=5 AND t301.a=t302.y;
    ]], {
        -- <where3-3.0>
        
        -- </where3-3.0>
    })

test:do_execsql_test(
    "where3-3.1",
    [[
        SELECT * FROM t301, t302 WHERE t302.x=5 AND t301.a=t302.y;
    ]], {
        -- <where3-3.1>
        
        -- </where3-3.1>
    })

test:do_execsql_test(
    "where3-3.2",
    [[
        SELECT * FROM t301 WHERE c=3 AND a IS NULL;
    ]], {
        -- <where3-3.2>
        
        -- </where3-3.2>
    })

test:do_execsql_test(
    "where3-3.3",
    [[
        SELECT * FROM t301 WHERE c=3 AND a IS NOT NULL;
    ]], {
        -- <where3-3.3>
        1, 2, 3, 2, 2, 3
        -- </where3-3.3>
    })

if 0
 then
    -- Query planner no longer does this
    -- Verify that when there are multiple tables in a join which must be
    -- full table scans that the query planner attempts put the table with
    -- the fewest number of output rows as the outer loop.
    --
    test:do_execsql_test(
        "where3-4.0",
        [[
            CREATE TABLE t400(a INTEGER PRIMARY KEY, b INT , c INT );
            CREATE TABLE t401(p INTEGER PRIMARY KEY, q INT , r INT );
            CREATE TABLE t402(x INTEGER PRIMARY KEY, y INT , z INT );
            EXPLAIN QUERY PLAN
            SELECT * FROM t400, t401, t402 WHERE t402.z LIKE 'abc%';
        ]], {
            -- <where3-4.0>
            0, 0, 2, "SCAN TABLE T402 (~983040 rows)", 0, 1, 0, "SCAN TABLE T400 (~1048576 rows)", 0, 2, 1, "SCAN TABLE T401 (~1048576 rows)"
            -- </where3-4.0>
        })

    test:do_execsql_test(
        "where3-4.1",
        [[
            EXPLAIN QUERY PLAN
            SELECT * FROM t400, t401, t402 WHERE t401.r LIKE 'abc%';
        ]], {
            -- <where3-4.1>
            0, 0, 1, "SCAN TABLE T401 (~983040 rows)", 0, 1, 0, "SCAN TABLE T400 (~1048576 rows)", 0, 2, 2, "SCAN TABLE T402 (~1048576 rows)"
            -- </where3-4.1>
        })

    test:do_execsql_test(
        "where3-4.2",
        [[
            EXPLAIN QUERY PLAN
            SELECT * FROM t400, t401, t402 WHERE t400.c LIKE 'abc%';
        ]], {
            -- <where3-4.2>
            0, 0, 0, "SCAN TABLE T400 (~983040 rows)", 0, 1, 1, "SCAN TABLE T401 (~1048576 rows)", 0, 2, 2, "SCAN TABLE T402 (~1048576 rows)"
            -- </where3-4.2>
        })

end
-- endif
-- Verify that a performance regression encountered by firefox
-- has been fixed.
--
test:do_execsql_test(
    "where3-5.0",
    [[
        CREATE TABLE aaa (id INTEGER PRIMARY KEY, type INTEGER,
                          fk TEXT DEFAULT NULL, parent INTEGER,
                          position INTEGER, title TEXT,
                          keyword_id INTEGER, folder_type TEXT,
                          dateAdded INTEGER, lastModified INTEGER);
        CREATE INDEX aaa_111 ON aaa (fk, type);
        CREATE INDEX aaa_222 ON aaa (parent, position);
        CREATE INDEX aaa_333 ON aaa (fk, lastModified);
        CREATE TABLE bbb (id INTEGER PRIMARY KEY, type INTEGER,
                          fk TEXT DEFAULT NULL, parent INTEGER,
                          position INTEGER, title TEXT,
                          keyword_id INTEGER, folder_type TEXT,
                          dateAdded INTEGER, lastModified INTEGER);
        CREATE INDEX bbb_111 ON bbb (fk, type);
        CREATE INDEX bbb_222 ON bbb (parent, position);
        CREATE INDEX bbb_333 ON bbb (fk, lastModified);

         SELECT bbb.title AS tag_title 
           FROM aaa JOIN bbb ON bbb.id = aaa.parent  
          WHERE aaa.fk = 'constant'
            AND LENGTH(bbb.title) > 0
            AND bbb.parent = 4
          ORDER BY bbb.title COLLATE "unicode_ci" ASC;
    ]], {
        -- <where3-5.0>
        
        -- </where3-5.0>
    })

-- do_execsql_test where3-5.1 {
--   EXPLAIN QUERY PLAN
--    SELECT bbb.title AS tag_title 
--      FROM aaa JOIN aaa AS bbb ON bbb.id = aaa.parent  
--     WHERE aaa.fk = 'constant'
--       AND LENGTH(bbb.title) > 0
--       AND bbb.parent = 4
--     ORDER BY bbb.title COLLATE NOCASE ASC;
-- } {
--   0 0 0 {SEARCH TABLE aaa USING INDEX aaa_333 (fk=?)} 
--   0 1 1 {SEARCH TABLE aaa AS bbb USING INTEGER PRIMARY KEY (rowid=?)} 
--   0 0 0 {USE TEMP B-TREE FOR ORDER BY}
-- }
-- do_execsql_test where3-5.2 {
--   EXPLAIN QUERY PLAN
--    SELECT bbb.title AS tag_title 
--      FROM bbb JOIN aaa ON bbb.id = aaa.parent  
--     WHERE aaa.fk = 'constant'
--       AND LENGTH(bbb.title) > 0
--       AND bbb.parent = 4
--     ORDER BY bbb.title COLLATE NOCASE ASC;
-- } {
--   0 0 1 {SEARCH TABLE aaa USING INDEX aaa_333 (fk=?)} 
--   0 1 0 {SEARCH TABLE bbb USING INTEGER PRIMARY KEY (rowid=?)} 
--   0 0 0 {USE TEMP B-TREE FOR ORDER BY}
-- }
-- do_execsql_test where3-5.3 {
--   EXPLAIN QUERY PLAN
--    SELECT bbb.title AS tag_title 
--      FROM aaa AS bbb JOIN aaa ON bbb.id = aaa.parent  
--     WHERE aaa.fk = 'constant'
--       AND LENGTH(bbb.title) > 0
--       AND bbb.parent = 4
--     ORDER BY bbb.title COLLATE NOCASE ASC;
-- } {
--   0 0 1 {SEARCH TABLE aaa USING INDEX aaa_333 (fk=?)} 
--   0 1 0 {SEARCH TABLE aaa AS bbb USING INTEGER PRIMARY KEY (rowid=?)} 
--   0 0 0 {USE TEMP B-TREE FOR ORDER BY}
-- }
-- Name resolution with NATURAL JOIN and USING
--
test:do_test(
    "where3-6.setup",
    function()
        return test:execsql [[
            CREATE TABLE t6w(a  INT primary key, w TEXT);
            INSERT INTO t6w VALUES(1, 'w-one');
            INSERT INTO t6w VALUES(2, 'w-two');
            INSERT INTO t6w VALUES(9, 'w-nine');
            CREATE TABLE t6x(a  INT primary key, x TEXT);
            INSERT INTO t6x VALUES(1, 'x-one');
            INSERT INTO t6x VALUES(3, 'x-three');
            INSERT INTO t6x VALUES(9, 'x-nine');
            CREATE TABLE t6y(a  INT primary key, y TEXT);
            INSERT INTO t6y VALUES(1, 'y-one');
            INSERT INTO t6y VALUES(4, 'y-four');
            INSERT INTO t6y VALUES(9, 'y-nine');
            CREATE TABLE t6z(a  INT primary key, z TEXT);
            INSERT INTO t6z VALUES(1, 'z-one');
            INSERT INTO t6z VALUES(5, 'z-five');
            INSERT INTO t6z VALUES(9, 'z-nine');
        ]]
    end, {
        -- <where3-6.setup>
        
        -- </where3-6.setup>
    })

local predicates = {"",
                    "ORDER BY a",
                    "ORDER BY t6w.a",
                    "WHERE a>0",
                    "WHERE t6y.a>0",
                    "WHERE a>0 ORDER BY a"}
for cnt, predicate in ipairs(predicates) do
    test:do_test(
        "where3-6."..cnt..".1",
        function()
            local sql = "SELECT * FROM t6w NATURAL JOIN t6x NATURAL JOIN t6y"
            sql = sql .. " NATURAL JOIN t6z "
            sql = sql .. predicate
            return test:execsql(sql)
        end, {
            1, "w-one", "x-one", "y-one", "z-one", 9, "w-nine", "x-nine", "y-nine", "z-nine"
        })

    test:do_test(
        "where3-6."..cnt..".2",
        function()
            local sql = "SELECT * FROM t6w JOIN t6x USING(a) JOIN t6y USING(a)"
            sql = sql .. " JOIN t6z USING(a) "
            sql = sql .. predicate
            return test:execsql(sql)
        end, {
            1, "w-one", "x-one", "y-one", "z-one", 9, "w-nine", "x-nine", "y-nine", "z-nine"
        })

    test:do_test(
        "where3-6."..cnt..".3",
        function()
            local sql = "SELECT * FROM t6w NATURAL JOIN t6x JOIN t6y USING(a)"
            sql = sql .. " JOIN t6z USING(a) "
            sql = sql .. predicate
            return test:execsql(sql)
        end, {
            1, "w-one", "x-one", "y-one", "z-one", 9, "w-nine", "x-nine", "y-nine", "z-nine"
        })

    test:do_test(
        "where3-6."..cnt..".4",
        function()
            local sql = "SELECT * FROM t6w JOIN t6x USING(a) NATURAL JOIN t6y"
            sql = sql .. " JOIN t6z USING(a) "
            sql = sql .. predicate
            return test:execsql(sql)
        end, {
            1, "w-one", "x-one", "y-one", "z-one", 9, "w-nine", "x-nine", "y-nine", "z-nine"
        })

    test:do_test(
        "where3-6."..cnt..".5",
        function()
            local sql = "SELECT * FROM t6w JOIN t6x USING(a) JOIN t6y USING(a)"
            sql = sql .. " NATURAL JOIN t6z "
            sql = sql .. predicate
            return test:execsql(sql)
        end, {
            1, "w-one", "x-one", "y-one", "z-one", 9, "w-nine", "x-nine", "y-nine", "z-nine"
        })

    test:do_test(
        "where3-6."..cnt..".6",
        function()
            local sql = "SELECT * FROM t6w JOIN t6x USING(a) NATURAL JOIN t6y"
            sql = sql .. " NATURAL JOIN t6z "
            sql = sql .. predicate
            return test:execsql(sql)
        end, {
            1, "w-one", "x-one", "y-one", "z-one", 9, "w-nine", "x-nine", "y-nine", "z-nine"
        })

    test:do_test(
        "where3-6."..cnt..".7",
        function()
            local sql = "SELECT * FROM t6w NATURAL JOIN t6x JOIN t6y USING(a)"
            sql = sql .. " NATURAL JOIN t6z "
            sql = sql .. predicate
            return test:execsql(sql)
        end, {
            1, "w-one", "x-one", "y-one", "z-one", 9, "w-nine", "x-nine", "y-nine", "z-nine"
        })

    test:do_test(
        "where3-6."..cnt..".8",
        function()
            local sql = "SELECT * FROM t6w NATURAL JOIN t6x NATURAL JOIN t6y"
            sql = sql .. " JOIN t6z USING(a) "
            sql = sql .. predicate
            return test:execsql(sql)
        end, {
            1, "w-one", "x-one", "y-one", "z-one", 9, "w-nine", "x-nine", "y-nine", "z-nine"
        })

end
test:do_execsql_test(
    "where3-7-setup",
    [[
        CREATE TABLE t71(x1 INTEGER PRIMARY KEY, y1 INT );
        CREATE TABLE t72(x2 INTEGER PRIMARY KEY, y2 INT );
        CREATE TABLE t73(x3  INT primary key, y3 INT );
        CREATE TABLE t74(x4 INT , y4  INT primary key);
        INSERT INTO t71 VALUES(123,234);
        INSERT INTO t72 VALUES(234,345);
        INSERT INTO t73 VALUES(123,234);
        INSERT INTO t74 VALUES(234,345);
        INSERT INTO t74 VALUES(234,678);
    ]], {
        -- <where3-7-setup>
        
        -- </where3-7-setup>
    })

-- Tarantool: optimization control interface to Lua is not implemented yet
--for _ in X(0, "X!foreach", [=[["disabled_opt","none omit-noop-join all"]]=]) do
--    X(448, "X!cmd", [=[["optimization_control","db","all","1"]]=])
--    X(449, "X!cmd", [=[["optimization_control","db",["disabled_opt"],"0"]]=])
local disabled_opt = "none"
     test:do_execsql_test(
    "where3-7."..disabled_opt..".1",
        [[
            SELECT x1 FROM t71 LEFT JOIN t72 ON x2=y1;
        ]], {
            123
        })

    test:do_execsql_test(
        "where3-7."..disabled_opt..".2",
        [[
            SELECT x1 FROM t71 LEFT JOIN t72 ON x2=y1 WHERE y2 IS NULL;
        ]], {
            
        })

    test:do_execsql_test(
        "where3-7."..disabled_opt..".3",
        [[
            SELECT x1 FROM t71 LEFT JOIN t72 ON x2=y1 WHERE y2 IS NOT NULL;
        ]], {
            123
        })

    test:do_execsql_test(
        "where3-7."..disabled_opt..".4",
        [[
            SELECT x1 FROM t71 LEFT JOIN t72 ON x2=y1 AND y2 IS NULL;
        ]], {
            123
        })

    test:do_execsql_test(
        "where3-7."..disabled_opt..".5",
        [[
            SELECT x1 FROM t71 LEFT JOIN t72 ON x2=y1 AND y2 IS NOT NULL;
        ]], {
            123
        })

    test:do_execsql_test(
        "where3-7."..disabled_opt..".6",
        [[
            SELECT x3 FROM t73 LEFT JOIN t72 ON x2=y3;
        ]], {
            123
        })

    test:do_execsql_test(
        "where3-7."..disabled_opt..".7",
        [[
            SELECT DISTINCT x3 FROM t73 LEFT JOIN t72 ON x2=y3;
        ]], {
            123
        })

    test:do_execsql_test(
        "where3-7."..disabled_opt..".8",
        [[
            SELECT x3 FROM t73 LEFT JOIN t74 ON x4=y3;
        ]], {
            123, 123
        })

    test:do_execsql_test(
        "where3-7."..disabled_opt..".9",
        [[
            SELECT DISTINCT x3 FROM t73 LEFT JOIN t74 ON x4=y3;
        ]], {
            123
        })

-- end
test:finish_test()

