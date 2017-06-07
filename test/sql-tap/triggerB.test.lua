#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(201)

--!./tcltestrunner.lua
-- 2008 April 15
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice', here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for SQLite library. Specifically,
-- it tests updating tables with constraints within a trigger.  Ticket #3055.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


-- Create test tables with constraints.
--
test:do_execsql_test(
    "triggerB-1.1",
    [[
        CREATE TABLE x(x INTEGER PRIMARY KEY, y INT NOT NULL);
        INSERT INTO x VALUES(1, 1);
        INSERT INTO x VALUES(2, 1);
        CREATE TEMP VIEW vx AS SELECT x, y, 0 AS yy FROM x;
        CREATE TEMP TRIGGER tx INSTEAD OF UPDATE OF y ON vx
        BEGIN
          UPDATE x SET y = new.y WHERE x = new.x;
        END;
        SELECT * FROM vx;
    ]], {
        -- <triggerB-1.1>
        1, 1, 0, 2, 1, 0
        -- </triggerB-1.1>
    })

-- MUST_WORK_TEST 
-- do_test triggerB-1.2 {
--   execsql {
--     UPDATE vx SET y = yy;
--     SELECT * FROM vx;
--   }
-- } {1 0 0 2 0 0}
-- Added 2008-08-22:
--
-- Name resolution within triggers.
--
test:do_catchsql_test(
    "triggerB-2.1",
    [[
        CREATE TRIGGER ty AFTER INSERT ON x BEGIN
           SELECT wen.x; -- Unrecognized name
        END;
        INSERT INTO x VALUES(1,2);
    ]], {
        -- <triggerB-2.1>
        1, "no such column: wen.x"
        -- </triggerB-2.1>
    })

test:do_catchsql_test(
    "triggerB-2.2",
    [[
        CREATE TRIGGER tz AFTER UPDATE ON x BEGIN
           SELECT dlo.x; -- Unrecognized name
        END;
        UPDATE x SET y=y+1;
    ]], {
        -- <triggerB-2.2>
        1, "no such column: dlo.x"
        -- </triggerB-2.2>
    })

test:do_test(
    "triggerB-2.3",
    function()
        test:execsql [[
            CREATE TABLE t2(a INTEGER PRIMARY KEY, b);
            INSERT INTO t2 VALUES(1,2);
            CREATE TABLE changes(x PRIMARY KEY,y);
            CREATE TRIGGER r1t2 AFTER UPDATE ON t2 BEGIN
              INSERT INTO changes VALUES(new.a, new.b);
            END;
        ]]
        return test:execsql [[
            UPDATE t2 SET a=a+10;
            SELECT * FROM changes;
        ]]
    end, {
        -- <triggerB-2.3>
        11, 2
        -- </triggerB-2.3>
    })

test:do_test(
    "triggerB-2.4",
    function()
        test:execsql [[
            CREATE TRIGGER r2t2 AFTER DELETE ON t2 BEGIN
              INSERT INTO changes VALUES(old.a, old.c);
            END;
        ]]
        return test:catchsql [[
            DELETE FROM t2;
        ]]
    end, {
        -- <triggerB-2.4>
        1, "no such column: old.c"
        -- </triggerB-2.4>
    })

-- Triggers maintain a mask of columns from the invoking table that are
-- used in the trigger body as NEW.column or OLD.column.  That mask is then
-- used to reduce the amount of information that needs to be loaded into
-- the NEW and OLD pseudo-tables at run-time.
--
-- These tests cases check the logic for when there are many columns - more
-- than will fit in a bitmask.
--
test:do_test(
    "triggerB-3.1",
    function()
        test:execsql [[
            CREATE TABLE t3(
               c0 PRIMARY KEY,  c1,  c2,  c3,  c4,  c5,  c6,  c7,  c8,  c9,
               c10, c11, c12, c13, c14, c15, c16, c17, c18, c19,
               c20, c21, c22, c23, c24, c25, c26, c27, c28, c29,
               c30, c31, c32, c33, c34, c35, c36, c37, c38, c39,
               c40, c41, c42, c43, c44, c45, c46, c47, c48, c49,
               c50, c51, c52, c53, c54, c55, c56, c57, c58, c59,
               c60, c61, c62, c63, c64, c65
            );
            CREATE TABLE t3_changes(colnum PRIMARY KEY, oldval, newval);
            INSERT INTO t3 VALUES(
               'a0', 'a1', 'a2', 'a3', 'a4', 'a5', 'a6', 'a7', 'a8', 'a9',
               'a10','a11','a12','a13','a14','a15','a16','a17','a18','a19',
               'a20','a21','a22','a23','a24','a25','a26','a27','a28','a29',
               'a30','a31','a32','a33','a34','a35','a36','a37','a38','a39',
               'a40','a41','a42','a43','a44','a45','a46','a47','a48','a49',
               'a50','a51','a52','a53','a54','a55','a56','a57','a58','a59',
               'a60','a61','a62','a63','a64','a65'
            );
        ]]
        -- for _ in X(0, "X!for", [=[["set i 0","$i<=65","incr i"]]=]) do
        for i=0,65 do    
            sql = string.format([[
                    CREATE TRIGGER t3c%s AFTER UPDATE ON t3
                      WHEN old.c%s!=new.c%s BEGIN
                      INSERT INTO t3_changes VALUES(%s, old.c%s, new.c%s);
                    END
                  ]], i, i, i, i, i, i)
            test:execsql(sql)
        end
        return test:execsql [[
            SELECT * FROM t3_changes
        ]]
    end, {
        -- <triggerB-3.1>
        
        -- </triggerB-3.1>
    })

-- for _ in X(0, "X!for", [=[["set i 0","$i<=64","incr i"]]=]) do
for i=0,64 do    
--    X(139, "X!cmd", [=[["do_test",["triggerB-3.2.",["i"],".1"],["\n    execsql {\n      UPDATE t3 SET c",["i"],"='b",["i"],"';\n      SELECT * FROM t3_changes ORDER BY colnum DESC LIMIT 1;\n    }\n  "],[["i"]," a",["i"]," b",["i"]]]]=])
    test:do_execsql_test("triggerB-3.2."..i..".1",
                         string.format([[UPDATE t3 SET c%d='b%d'; 
                                         SELECT * FROM t3_changes ORDER BY colnum DESC LIMIT 1; ]],
                                       i, i),
                         {i, string.format("a%d", i), string.format("b%d", i)})
    test:do_execsql_test(
        "triggerB-3.2."..i..".2",
        [[
            SELECT count(*) FROM t3_changes
        ]], {
            (i + 1)
        })

    -- X(150, "X!cmd", [=[["do_test",["triggerB-3.2.",["i"],".2"],["\n    execsql {\n      SELECT * FROM t3_changes WHERE colnum=",["i"],"\n    }\n  "],[["i"]," a",["i"]," b",["i"]]]]=])
    test:do_execsql_test("triggerB-3.2."..i..".2",
                    string.format([[SELECT * FROM t3_changes WHERE colnum=%d]], i),
                    {i, string.format("a%d", i), string.format("b%d", i)})
end
test:finish_test()

