#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(22)

--!./tcltestrunner.lua
-- 2012 September 18
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
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
test:do_execsql_test(
    "in5-1.1",
    [[
        CREATE TABLE t1x(x INTEGER PRIMARY KEY);
        INSERT INTO t1x VALUES(1),(3),(5),(7),(9);
        CREATE TABLE t1y(y INTEGER PRIMARY KEY);
        INSERT INTO t1y VALUES(2),(4),(6),(8);
        CREATE TABLE t1z(z TEXT PRIMARY KEY);
        INSERT INTO t1z VALUES('a'),('c'),('e'),('g');
        CREATE TABLE t2(a INTEGER, b INTEGER, c TEXT, d TEXT, PRIMARY KEY(a,b,c));
        INSERT INTO t2 VALUES(1,2,'a','12a'),(1,2,'b','12b'),
                             (2,3,'g','23g'),(3,5,'c','35c'),
                             (4,6,'h','46h'),(5,6,'e','56e');
        --CREATE TABLE t3x AS SELECT x FROM t1x;
        CREATE TABLE t3x (x INTEGER PRIMARY KEY);
        INSERT INTO t3x SELECT x FROM t1x;

        --CREATE TABLE t3y AS SELECT y FROM t1y;
        CREATE TABLE t3y (y INTEGER PRIMARY KEY);
        INSERT INTO t3y SELECT y FROM t1y;

        --CREATE TABLE t3z AS SELECT z FROM t1z;
        CREATE TABLE t3z (z TEXT PRIMARY KEY);
        INSERT INTO t3z SELECT z FROM t1z;

        SELECT d FROM t2 WHERE a IN t1x AND b IN t1y AND c IN t1z ORDER BY c;
    ]], {
        -- <in5-1.1>
        "12a", "56e"
        -- </in5-1.1>
    })

test:do_execsql_test(
    "in5-1.2",
    [[
        SELECT d FROM t2 WHERE a IN t1y AND b IN t1x AND c IN t1z ORDER BY d;
    ]], {
        -- <in5-1.2>
        "23g"
        -- </in5-1.2>
    })

test:do_execsql_test(
    "in5-1.3",
    [[
        SELECT d FROM t2 WHERE a IN t3x AND b IN t3y AND c IN t3z ORDER BY d;
    ]], {
        -- <in5-1.3>
        "12a", "56e"
        -- </in5-1.3>
    })

test:do_execsql_test(
    "in5-2.1",
    [[
        CREATE INDEX t2abc ON t2(a,b,c);
        SELECT d FROM t2 WHERE a IN t1x AND b IN t1y AND c IN t1z ORDER BY d;
    ]], {
        -- <in5-2.1>
        "12a", "56e"
        -- </in5-2.1>
    })

test:do_execsql_test(
    "in5-2.2",
    [[
        SELECT d FROM t2 WHERE a IN t1y AND b IN t1x AND c IN t1z ORDER BY d;
    ]], {
        -- <in5-2.2>
        "23g"
        -- </in5-2.2>
    })

test:do_test(
    "in5-2.3",
    function()
        local nEph = 0
        for _, op in ipairs(test:execsql("EXPLAIN SELECT d FROM t2 WHERE a IN t1x AND b IN t1y AND c IN t1z")) do
            if (string.find(op, "OpenEphemeral"))
            then
                nEph = nEph + 1
            end
        end
        return nEph
        -- return X(71, "X!cmd", [=[["regexp","OpenEphemeral",[["db","eval","\n    EXPLAIN SELECT d FROM t2 WHERE a IN t1x AND b IN t1y AND c IN t1z\n  "]]]]=])
    end,
        -- <in5-2.3>
        0
        -- </in5-2.3>
    )

test:do_execsql_test(
    "in5-2.4",
    [[
        SELECT d FROM t2 WHERE a IN t3x AND b IN t3y AND c IN t3z ORDER BY d;
    ]], {
        -- <in5-2.4>
        "12a", "56e"
        -- </in5-2.4>
    })

-- Tarantool: Use of PK instead of `rowid` changed generated program,
-- so, `OpenEphemeral` is no longer emitted.
-- do_test in5-2.5.1 {
--  regexp {OpenEphemeral} [db eval {
--    EXPLAIN SELECT d FROM t2 WHERE a IN t3x AND b IN t1y AND c IN t1z
--  }]
--} {1}
--do_test in5-2.5.2 {
--  regexp {OpenEphemeral} [db eval {
--    EXPLAIN SELECT d FROM t2 WHERE a IN t1x AND b IN t3y AND c IN t1z
--  }]
--} {1}
--do_test in5-2.5.3 {
--  regexp {OpenEphemeral} [db eval {
--    EXPLAIN SELECT d FROM t2 WHERE a IN t1x AND b IN t1y AND c IN t3z
--  }]
--} {1}
test:do_execsql_test(
    "in5-3.1",
    [[
        DROP INDEX t2abc ON t2;
        CREATE INDEX t2ab ON t2(a,b);
        SELECT d FROM t2 WHERE a IN t1x AND b IN t1y AND c IN t1z ORDER BY d;
    ]], {
        -- <in5-3.1>
        "12a", "56e"
        -- </in5-3.1>
    })

test:do_execsql_test(
    "in5-3.2",
    [[
        SELECT d FROM t2 WHERE a IN t1y AND b IN t1x AND c IN t1z ORDER BY d;
    ]], {
        -- <in5-3.2>
        "23g"
        -- </in5-3.2>
    })

test:do_test(
    "in5-3.3",
    function()
        local nEph = 0
        for _, op in ipairs(test:execsql("EXPLAIN SELECT d FROM t2 WHERE a IN t1x AND b IN t1y AND c IN t1z")) do
            if (string.find(op, "OpenEphemeral"))
            then
                nEph = nEph + 1
            end
        end
        return nEph
        -- return X(111, "X!cmd", [=[["regexp","OpenEphemeral",[["db","eval","\n    EXPLAIN SELECT d FROM t2 WHERE a IN t1x AND b IN t1y AND c IN t1z\n  "]]]]=])
    end,
        -- <in5-3.3>
        0
        -- </in5-3.3>
    )

test:do_execsql_test(
    "in5-4.1",
    [[
        DROP INDEX t2ab ON t2;
        CREATE INDEX t2abcd ON t2(a,b,c,d);
        SELECT d FROM t2 WHERE a IN t1x AND b IN t1y AND c IN t1z ORDER BY d;
    ]], {
        -- <in5-4.1>
        "12a", "56e"
        -- </in5-4.1>
    })

test:do_execsql_test(
    "in5-4.2",
    [[
        SELECT d FROM t2 WHERE a IN t1y AND b IN t1x AND c IN t1z ORDER BY d;
    ]], {
        -- <in5-4.2>
        "23g"
        -- </in5-4.2>
    })

test:do_test(
    "in5-4.3",
    function()
        local nEph = 0
        for _, op in ipairs(test:execsql("EXPLAIN SELECT d FROM t2 WHERE a IN t1x AND b IN t1y AND c IN t1z")) do
            if (string.find(op, "OpenEphemeral"))
            then
                nEph = nEph + 1
            end
        end
        return nEph
        -- return X(129, "X!cmd", [=[["regexp","OpenEphemeral",[["db","eval","\n    EXPLAIN SELECT d FROM t2 WHERE a IN t1x AND b IN t1y AND c IN t1z\n  "]]]]=])
    end,
        -- <in5-4.3>
        0
        -- </in5-4.3>
    )

test:do_execsql_test(
    "in5-5.1",
    [[
        DROP INDEX t2abcd ON t2;
        CREATE INDEX t2cbad ON t2(c,b,a,d);
        SELECT d FROM t2 WHERE a IN t1x AND b IN t1y AND c IN t1z ORDER BY d;
    ]], {
        -- <in5-5.1>
        "12a", "56e"
        -- </in5-5.1>
    })

test:do_execsql_test(
    "in5-5.2",
    [[
        SELECT d FROM t2 WHERE a IN t1y AND b IN t1x AND c IN t1z ORDER BY d;
    ]], {
        -- <in5-5.2>
        "23g"
        -- </in5-5.2>
    })

test:do_test(
    "in5-5.3",
    function()
        local nEph = 0
        for _, op in ipairs(test:execsql("EXPLAIN SELECT d FROM t2 WHERE a IN t1x AND b IN t1y AND c IN t1z")) do
            if (string.find(op, "OpenEphemeral"))
            then
                nEph = nEph + 1
            end
        end
        return nEph
        -- return X(148, "X!cmd", [=[["regexp","OpenEphemeral",[["db","eval","\n    EXPLAIN SELECT d FROM t2 WHERE a IN t1x AND b IN t1y AND c IN t1z\n  "]]]]=])
    end,
        -- <in5-5.3>
        0
        -- </in5-5.3>
    )

---------------------------------------------------------------------------
-- At one point sql was removing the DISTINCT keyword from expressions
-- similar to:
--
--   <expr1> IN (SELECT DISTINCT <expr2> FROM...)
--
-- However, there are a few obscure cases where this is incorrect. For
-- example, if the SELECT features a LIMIT clause, or if the collation
-- sequence or affinity used by the DISTINCT does not match the one used
-- by the IN(...) expression.
--
test:do_execsql_test(
    "6.1.1",
    [[
        CREATE TABLE t1(id  INT primary key, a  TEXT COLLATE "unicode_ci");
        INSERT INTO t1 VALUES(1, 'one');
        INSERT INTO t1 VALUES(2, 'ONE');
    ]])

test:do_execsql_test(
    "6.1.2",
    [[
        SELECT COUNT(*) FROM t1 WHERE a COLLATE "binary"
        IN (SELECT DISTINCT a FROM t1);
    ]], {
        -- <6.1.2>
        1
        -- </6.1.2>
    })

test:do_execsql_test(
    "6.2.1",
    [[
        CREATE TABLE t3(a INT , b  INT PRIMARY KEY);
        INSERT INTO t3 VALUES(1, 1);
        INSERT INTO t3 VALUES(1, 2);
        INSERT INTO t3 VALUES(1, 3);
        INSERT INTO t3 VALUES(2, 4);
        INSERT INTO t3 VALUES(2, 5);
        INSERT INTO t3 VALUES(2, 6);
        INSERT INTO t3 VALUES(3, 7);
        INSERT INTO t3 VALUES(3, 8);
        INSERT INTO t3 VALUES(3, 9);
    ]])

test:do_execsql_test(
    "6.2.2",
    [[
        SELECT COUNT(*) FROM t3 WHERE b IN (SELECT DISTINCT a FROM t3 LIMIT 5);
    ]], {
        -- <6.2.2>
        3
        -- </6.2.2>
    })

test:do_execsql_test(
    "6.2.3",
    [[
        SELECT COUNT(*) FROM t3 WHERE b IN (SELECT a FROM t3 LIMIT 5);
    ]], {
        -- <6.2.3>
        2
        -- </6.2.3>
    })

test:do_execsql_test(
    "6.3.1",
    [[
        CREATE TABLE x1(pk  INT primary key, a INT );
        CREATE TABLE x2(pk  INT primary key, b INT );
        INSERT INTO x1 VALUES(1, 1), (2, 1), (3, 2);
        INSERT INTO x2 VALUES(1, 1), (2, 2);
        SELECT COUNT(*) FROM x2 WHERE b IN (SELECT DISTINCT a FROM x1 LIMIT 2);
    ]], {
        -- <6.3.1>
        2
        -- </6.3.1>
    })

test:finish_test()

