#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(5)

--!./tcltestrunner.lua
-- 2011 December 9
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library.
--
-- This file implements tests to verify that ticket [7bbfb7d442] has been
-- fixed.  
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
local testprefix = "tkt-7bbfb7d442"
-- MUST_WORK_TEST
if (1 > 0)
 then
    test:do_execsql_test(
        1.1,
        [[
            CREATE TABLE t1(id  INT primary key, a INT , b TEXT);
            INSERT INTO t1 VALUES(1, 1, 'one');
            INSERT INTO t1 VALUES(2, 2, 'two');
            INSERT INTO t1 VALUES(3, 3, 'three');

            CREATE TABLE t2(id  INT primary key, c TEXT, d TEXT);
            INSERT INTO t2 VALUES(1, 'one', 'I');
            INSERT INTO t2 VALUES(2, 'two', 'II');
            INSERT INTO t2 VALUES(3, 'three', 'III');

            CREATE TABLE t3(t3_a  INT PRIMARY KEY, t3_d TEXT);
            CREATE TRIGGER t3t AFTER INSERT ON t3 FOR EACH ROW
            WHEN new.t3_d IS NULL BEGIN
              UPDATE t3 SET t3_d = (
                SELECT d FROM 
                  (SELECT * FROM t2 WHERE (new.t3_a%2)=(id%2) LIMIT 10),
                  (SELECT * FROM t1 WHERE (new.t3_a%2)=(id%2) LIMIT 10)
                WHERE a = new.t3_a AND b = c
              ) WHERE t3_a = new.t3_a;
            END;
        ]])

    test:do_execsql_test(
        1.2,
        [[
            INSERT INTO t3(t3_a) VALUES(1);
            INSERT INTO t3(t3_a) VALUES(2);
            INSERT INTO t3(t3_a) VALUES(3);
            SELECT * FROM t3;
        ]], {
            -- <1.2>
            1, "I", 2, "II", 3, "III"
            -- </1.2>
        })

    test:do_execsql_test(
        1.3,
        [[
            DELETE FROM t3 
        ]])

    test:do_execsql_test(
        1.4,
        [[
            INSERT INTO t3(t3_a) SELECT 1 UNION SELECT 2 UNION SELECT 3;
            SELECT * FROM t3;
        ]], {
            -- <1.4>
            1, "I", 2, "II", 3, "III"
            -- </1.4>
        })



    ---------------------------------------------------------------------------
    -- The following test case - 2.* - is from the original bug report as 
    -- posted to the mailing list.
    --
    test:do_execsql_test(
        2.1,
        [[
            CREATE TABLE InventoryControl (
              InventoryControlId INTEGER,
              SKU INTEGER NOT NULL PRIMARY KEY,
              Variant INTEGER NOT NULL DEFAULT 0,
              ControlDate TEXT NOT NULL,
              ControlState INTEGER NOT NULL DEFAULT -1,
              DeliveredQty TEXT
            );

            CREATE TRIGGER TGR_InventoryControl_AfterInsert
            AFTER INSERT ON InventoryControl 
            FOR EACH ROW WHEN NEW.ControlState=-1 BEGIN 

            INSERT OR REPLACE INTO InventoryControl(
                  InventoryControlId,SKU,Variant,ControlDate,ControlState,DeliveredQty
            ) SELECT
                    T1.InventoryControlId AS InventoryControlId,
                    T1.SKU AS SKU,
                    T1.Variant AS Variant,
                    T1.ControlDate AS ControlDate,
                    1 AS ControlState,
                    CAST(COALESCE(T2.DeliveredQty,0) AS STRING) AS DeliveredQty
                FROM (
                    SELECT
                        NEW.InventoryControlId AS InventoryControlId,
                        II.SKU AS SKU,
                        II.Variant AS Variant,
                        CAST(COALESCE(LastClosedIC.ControlDate,NEW.ControlDate) AS STRING) AS ControlDate
                    FROM
                        InventoryItem II
                    LEFT JOIN
                        InventoryControl LastClosedIC
                        ON  LastClosedIC.InventoryControlId IN ( SELECT 99999 )
                    WHERE
                        II.SKU=NEW.SKU AND
                        II.Variant=NEW.Variant
                )   T1
                LEFT JOIN (
                    SELECT
                        TD.SKU AS SKU,
                        TD.Variant AS Variant,
                        10 AS DeliveredQty
                    FROM
                        TransactionDetail TD
                    WHERE
                        TD.SKU=NEW.SKU AND
                        TD.Variant=NEW.Variant
                )   T2
                ON  T2.SKU=T1.SKU AND
                    T2.Variant=T1.Variant;
            END;

            CREATE TABLE InventoryItem (
              SKU INTEGER NOT NULL,
              Variant INTEGER NOT NULL DEFAULT 0,
              DeptCode INTEGER NOT NULL,
              GroupCode INTEGER NOT NULL,
              ItemDescription VARCHAR(120) NOT NULL,
              PRIMARY KEY(SKU, Variant)
            );

            INSERT INTO InventoryItem VALUES(220,0,1,170,'Scoth Tampon Recurer');
            INSERT INTO InventoryItem VALUES(31,0,1,110,'Fromage');

            CREATE TABLE TransactionDetail (
              TransactionId INTEGER NOT NULL,
              SKU INTEGER NOT NULL,
              Variant INTEGER NOT NULL DEFAULT 0,
              PRIMARY KEY(TransactionId, SKU, Variant)
            );
            INSERT INTO TransactionDetail(TransactionId, SKU, Variant) VALUES(44, 31, 0);


            INSERT INTO InventoryControl(SKU, Variant, ControlDate) SELECT 
                II.SKU AS SKU, II.Variant AS Variant, '2011-08-30' AS ControlDate
                FROM InventoryItem II;
        ]])

    -- MUST_WORK_TEST triggers
    if (0 > 0) then
        test:do_execsql_test(
            2.2,
            [[
                SELECT SKU, DeliveredQty FROM InventoryControl WHERE SKU=31
            ]], {
                -- <2.2>
                31, "10"
                -- </2.2>
            })

        test:do_execsql_test(
            2.3,
            [[
                SELECT CASE WHEN DeliveredQty=10 THEN "TEST PASSED!" ELSE "TEST FAILED!" END
                FROM InventoryControl WHERE SKU=31;
            ]], {
                -- <2.3>
                "TEST PASSED!"
                -- </2.3>
            })
    end

end
test:finish_test()

