#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(17)

--!./tcltestrunner.lua
-- 2008 February 12
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file implements regression tests for sql library. Specifically,
-- it tests issues relating to firing an INSTEAD OF trigger on a VIEW
-- when one tries to UPDATE or DELETE from the view.  Does the WHERE
-- clause of the UPDATE or DELETE statement get passed down correctly
-- into the query that manifests the view?
--
-- Ticket #2938
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- ifcapable !trigger||!compound {
--   finish_test
--   return
-- }
-- Create two table containing some sample data
--
test:do_test(
    "triggerA-1.1",
    function()
        test:execsql [[
            CREATE TABLE t1(x INTEGER PRIMARY KEY, y TEXT UNIQUE);
            CREATE TABLE t2(a INTEGER PRIMARY KEY, b INTEGER UNIQUE, c TEXT);
        ]]
        local i = 1
        local words = {"one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten"}
        for i, word in ipairs(words) do
        -- for _ in X(0, "X!foreach", [=[["word","one two three four five six seven eight nine ten"]]=]) do
            -- j = X(42, "X!cmd", [=[["expr","$i*100 + [string length $word]"]]=])
            local j = i * 100 + string.len(word)
            test:execsql(string.format([[
                INSERT INTO t1 VALUES(%d,'%s');
                INSERT INTO t2 VALUES(20-%d,%d,'%s');
            ]], i, word, i, j, word))
            i = i + 1
        end
        return test:execsql [[
            SELECT count(*) FROM t1 UNION ALL SELECT count(*) FROM t2;
        ]]
    end, {
        -- <triggerA-1.1>
        10, 10
        -- </triggerA-1.1>
    })

-- Create views of various forms against one or both of the two tables.
--
test:do_test(
    "triggerA-1.2",
    function()
        return test:execsql [[
            CREATE VIEW v1 AS SELECT y, x FROM t1;
            SELECT * FROM v1 ORDER BY 1;
        ]]
    end, {
        -- <triggerA-1.2>
        "eight", 8, "five", 5, "four", 4, "nine", 9, "one", 1, "seven", 7, "six", 6, "ten", 10, "three", 3, "two", 2
        -- </triggerA-1.2>
    })

test:do_test(
    "triggerA-1.3",
    function()
        return test:execsql [[
            CREATE VIEW v2 AS SELECT x, y FROM t1 WHERE y LIKE '%e%';
            SELECT * FROM v2 ORDER BY 1;
        ]]
    end, {
        -- <triggerA-1.3>
        1, "one", 3, "three", 5, "five", 7, "seven", 8, "eight", 9, "nine", 10, "ten"
        -- </triggerA-1.3>
    })

test:do_test(
    "triggerA-1.4",
    function()
        return test:execsql [[
            CREATE VIEW v3 AS
              SELECT CAST(x AS TEXT) AS c1 FROM t1 UNION SELECT y FROM t1;
            SELECT * FROM v3 ORDER BY c1;
        ]]
    end, {
        -- <triggerA-1.4>
        "1", "10", "2", "3", "4", "5", "6", "7", "8", "9", "eight", "five", "four", "nine", "one", "seven", "six", "ten", "three", "two"
        -- </triggerA-1.4>
    })

test:do_test(
    "triggerA-1.5",
    function()
        return test:execsql [[
            CREATE VIEW v4 AS
               SELECT CAST(x AS TEXT) AS c1 FROM t1
               UNION SELECT y FROM t1 WHERE x BETWEEN 3 and 5;
            SELECT * FROM v4 ORDER BY 1;
        ]]
    end, {
        -- <triggerA-1.5>
        "1", "10", "2", "3", "4", "5", "6", "7", "8", "9", "five", "four", "three"
        -- </triggerA-1.5>
    })

test:do_test(
    "triggerA-1.6",
    function()
        return test:execsql [[
            CREATE VIEW v5 AS SELECT x, b FROM t1, t2 WHERE y=c;
            SELECT * FROM v5 ORDER BY x DESC;
        ]]
    end, {
        -- <triggerA-1.6>
        10, 1003, 9, 904, 8, 805, 7, 705, 6, 603, 5, 504, 4, 404, 3, 305, 2, 203, 1, 103
        -- </triggerA-1.6>
    })

-- Create INSTEAD OF triggers on the views.  Run UPDATE and DELETE statements
-- using those triggers.  Verify correct operation.
--
test:do_test(
    "triggerA-2.1",
    function()
        return test:execsql [[
            CREATE TABLE result2(id INTEGER PRIMARY KEY, a TEXT,b INT);
            CREATE TRIGGER r1d INSTEAD OF DELETE ON v1 FOR EACH ROW BEGIN
              INSERT INTO result2(id, a,b) VALUES((SELECT coalesce(max(id),0) + 1 FROM result2),
                                                  old.y, old.x);
            END;
            DELETE FROM v1 WHERE x=5;
            SELECT a, b FROM result2;
        ]]
    end, {
        -- <triggerA-2.1>
        "five", 5
        -- </triggerA-2.1>
    })

test:do_test(
    "triggerA-2.2",
    function()
        return test:execsql [[
            CREATE TABLE result4(id INTEGER PRIMARY KEY, a TEXT,b INT,c TEXT,d INT);
            CREATE TRIGGER r1u INSTEAD OF UPDATE ON v1 FOR EACH ROW BEGIN
              INSERT INTO result4(id, a,b,c,d) VALUES((SELECT coalesce(max(id),0) + 1 FROM result4),
                                                      old.y, old.x, new.y, new.x);
            END;
            UPDATE v1 SET y=y||'-extra' WHERE x BETWEEN 3 AND 5;
            SELECT a,b,c,d FROM result4 ORDER BY a;
        ]]
    end, {
        -- <triggerA-2.2>
        "five", 5, "five-extra", 5, "four", 4, "four-extra", 4, "three", 3, "three-extra", 3
        -- </triggerA-2.2>
    })

test:do_test(
    "triggerA-2.3",
    function()
        return test:execsql [[
            DELETE FROM result2;
            CREATE TRIGGER r2d INSTEAD OF DELETE ON v2 FOR EACH ROW BEGIN
              INSERT INTO result2(id, a,b) VALUES((SELECT coalesce(max(id),0) + 1 FROM result2),
                                                  old.y, old.x);
            END;
            DELETE FROM v2 WHERE x=5;
            SELECT a, b FROM result2;
        ]]
    end, {
        -- <triggerA-2.3>
        "five", 5
        -- </triggerA-2.3>
    })

test:do_test(
    "triggerA-2.4",
    function()
        return test:execsql [[
            DELETE FROM result4;
            CREATE TRIGGER r2u INSTEAD OF UPDATE ON v2 FOR EACH ROW BEGIN
              INSERT INTO result4(id, a,b,c,d) VALUES((SELECT coalesce(max(id),0) + 1 FROM result4),
                                                      old.y, old.x, new.y, new.x);
            END;
            UPDATE v2 SET y=y||'-extra' WHERE x BETWEEN 3 AND 5;
            SELECT a,b,c,d FROM result4 ORDER BY a;
        ]]
    end, {
        -- <triggerA-2.4>
        "five", 5, "five-extra", 5, "three", 3, "three-extra", 3
        -- </triggerA-2.4>
    })

test:do_test(
    "triggerA-2.5",
    function()
        return test:execsql [[
            CREATE TABLE result1(id INTEGER PRIMARY KEY, a TEXT);
            CREATE TRIGGER r3d INSTEAD OF DELETE ON v3 FOR EACH ROW BEGIN
              INSERT INTO result1(id, a) VALUES((SELECT coalesce(max(id),0) + 1 FROM result1),
                                                old.c1);
            END;
            DELETE FROM v3 WHERE c1 BETWEEN '8' AND 'eight';
            SELECT a FROM result1 ORDER BY a;
        ]]
    end, {
        -- <triggerA-2.5>
        "8", "9", "eight"
        -- </triggerA-2.5>
    })

test:do_test(
    "triggerA-2.6",
    function()
        return test:execsql [[
            DROP TABLE result2;
            CREATE TABLE result2(id INTEGER PRIMARY KEY, a TEXT,b TEXT);
            CREATE TRIGGER r3u INSTEAD OF UPDATE ON v3 FOR EACH ROW BEGIN
              INSERT INTO result2(id, a,b) VALUES((SELECT coalesce(max(id),0) + 1 FROM result2),
                                                  old.c1, new.c1);
            END;
            UPDATE v3 SET c1 = c1 || '-extra' WHERE c1 BETWEEN '8' and 'eight';
            SELECT a, b FROM result2 ORDER BY a;
        ]]
    end, {
        -- <triggerA-2.6>
        "8", "8-extra", "9", "9-extra", "eight", "eight-extra"
        -- </triggerA-2.6>
    })

test:do_test(
    "triggerA-2.7",
    function()
        return test:execsql [[
            DELETE FROM result1;
            CREATE TRIGGER r4d INSTEAD OF DELETE ON v4 FOR EACH ROW BEGIN
              INSERT INTO result1(id, a) VALUES((SELECT coalesce(max(id),0) + 1 FROM result1),
                                                old.c1);
            END;
            DELETE FROM v4 WHERE c1 BETWEEN '8' AND 'eight';
            SELECT a FROM result1 ORDER BY a;
        ]]
    end, {
        -- <triggerA-2.7>
        "8", "9"
        -- </triggerA-2.7>
    })

test:do_test(
    "triggerA-2.8",
    function()
        return test:execsql [[
            DELETE FROM result2;
            CREATE TRIGGER r4u INSTEAD OF UPDATE ON v4 FOR EACH ROW BEGIN
              INSERT INTO result2(id, a,b) VALUES((SELECT coalesce(max(id),0) + 1 FROM result2),
                                                  old.c1, new.c1);
            END;
            UPDATE v4 SET c1 = c1 || '-extra' WHERE c1 BETWEEN '8' and 'eight';
            SELECT a, b FROM result2 ORDER BY a;
        ]]
    end, {
        -- <triggerA-2.8>
        "8", "8-extra", "9", "9-extra"
        -- </triggerA-2.8>
    })

test:do_test(
    "triggerA-2.9",
    function()
        return test:execsql [[
            DROP TABLE result2;
            CREATE TABLE result2(id INTEGER PRIMARY KEY, a TEXT,b INT);
            CREATE TRIGGER r5d INSTEAD OF DELETE ON v5 FOR EACH ROW BEGIN
              INSERT INTO result2(id, a,b) VALUES((SELECT coalesce(max(id),0) + 1 FROM result2),
                                                  CAST(old.x AS STRING), old.b);
            END;
            DELETE FROM v5 WHERE x=5;
            SELECT a, b FROM result2;
        ]]
    end, {
        -- <triggerA-2.9>
        "5", 504
        -- </triggerA-2.9>
    })

test:do_test(
    "triggerA-2.10",
    function()
        return test:execsql [[
            DELETE FROM result4;
            CREATE TRIGGER r5u INSTEAD OF UPDATE ON v5 FOR EACH ROW BEGIN
              INSERT INTO result4(id, a,b,c,d) VALUES((SELECT coalesce(max(id),0) + 1 FROM result4),
                                                      CAST(old.x AS STRING), old.b, CAST(new.x AS STRING), new.b);
            END;
            UPDATE v5 SET b = b+9900000 WHERE x BETWEEN 3 AND 5;
            SELECT a,b,c,d FROM result4 ORDER BY a;
        ]]
    end, {
        -- <triggerA-2.10>
        "3", 305, "3", 9900305, "4", 404, "4", 9900404, "5", 504, "5", 9900504
        -- </triggerA-2.10>
    })

test:do_test(
    "triggerA-2.11",
    function()
        return test:execsql [[
            DELETE FROM result4;
            UPDATE v5 SET b = v5.b+9900000 WHERE v5.x BETWEEN 3 AND 5;
            SELECT a,b,c,d FROM result4 ORDER BY a;
        ]]
    end, {
        -- <triggerA-2.11>
        "3", 305, "3", 9900305, "4", 404, "4", 9900404, "5", 504, "5", 9900504
        -- </triggerA-2.11>
    })

test:finish_test()
