#!/usr/bin/env tarantool
local test = require("sqltester")
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
-- This file implements regression tests for sql library. Specifically,
-- it tests updating tables with constraints within a trigger.  Ticket #3055.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


-- Create test tables with constraints.
--
test:do_execsql_test(
    "triggerB-1.1",
    [[
        CREATE TABLE x(id INTEGER PRIMARY KEY, x INTEGER UNIQUE, y INT NOT NULL);
        INSERT INTO x VALUES(1, 1, 1);
        INSERT INTO x VALUES(2, 2, 1);
        CREATE VIEW vx AS SELECT x, y, 0 AS yy FROM x;
        CREATE TRIGGER tx INSTEAD OF UPDATE OF y ON vx
        FOR EACH ROW
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
        CREATE TRIGGER ty AFTER INSERT ON x FOR EACH ROW
        BEGIN
           SELECT wen.x; -- Unrecognized name
        END;
        INSERT INTO x VALUES(3,1,2);
    ]], {
        -- <triggerB-2.1>
        1, "Field 'X' was not found in space 'WEN' format"
        -- </triggerB-2.1>
    })

test:do_catchsql_test(
    "triggerB-2.2",
    [[
        CREATE TRIGGER tz AFTER UPDATE ON x FOR EACH ROW
        BEGIN
           SELECT dlo.x; -- Unrecognized name
        END;
        UPDATE x SET y=y+1;
    ]], {
        -- <triggerB-2.2>
        1, "Field 'X' was not found in space 'DLO' format"
        -- </triggerB-2.2>
    })

test:do_test(
    "triggerB-2.3",
    function()
        test:execsql [[
            CREATE TABLE t2(id INTEGER PRIMARY KEY, a INTEGER UNIQUE, b INT );
            INSERT INTO t2 VALUES(1, 1,2);
            CREATE TABLE changes(x  INT PRIMARY KEY,y INT );
            CREATE TRIGGER r1t2 AFTER UPDATE ON t2 FOR EACH ROW
            BEGIN
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
            CREATE TRIGGER r2t2 AFTER DELETE ON t2 FOR EACH ROW
            BEGIN
              INSERT INTO changes VALUES(old.a, old.c);
            END;
        ]]
        return test:catchsql [[
            DELETE FROM t2;
        ]]
    end, {
        -- <triggerB-2.4>
        1, "Field 'C' was not found in space 'OLD' format"
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
               id INT PRIMARY KEY, c0 TEXT UNIQUE,  c1 TEXT ,  c2 TEXT ,  c3 TEXT ,  c4 TEXT ,  c5 TEXT ,  c6 TEXT ,  c7 TEXT ,
               c8 TEXT ,  c9 TEXT , c10 TEXT , c11 TEXT , c12 TEXT , c13 TEXT , c14 TEXT , c15 TEXT , c16 TEXT , c17 TEXT ,
               c18 TEXT , c19 TEXT , c20 TEXT , c21 TEXT , c22 TEXT , c23 TEXT , c24 TEXT , c25 TEXT , c26 TEXT , c27 TEXT ,
               c28 TEXT , c29 TEXT , c30 TEXT , c31 TEXT , c32 TEXT , c33 TEXT , c34 TEXT , c35 TEXT , c36 TEXT , c37 TEXT ,
               c38 TEXT , c39 TEXT , c40 TEXT , c41 TEXT , c42 TEXT , c43 TEXT , c44 TEXT , c45 TEXT , c46 TEXT , c47 TEXT ,
               c48 TEXT , c49 TEXT , c50 TEXT , c51 TEXT , c52 TEXT , c53 TEXT , c54 TEXT , c55 TEXT , c56 TEXT , c57 TEXT ,
               c58 TEXT , c59 TEXT , c60 TEXT , c61 TEXT , c62 TEXT , c63 TEXT , c64 TEXT , c65 TEXT
            );
            CREATE TABLE t3_changes(colnum INT PRIMARY KEY, oldval TEXT , newval TEXT );
            INSERT INTO t3 VALUES(
               0, 'a0', 'a1', 'a2', 'a3', 'a4', 'a5', 'a6', 'a7', 'a8', 'a9',
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
            local sql = string.format([[
                    CREATE TRIGGER t3c%s AFTER UPDATE ON t3
                    FOR EACH ROW
                    WHEN old.c%s!=new.c%s
                    BEGIN
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

