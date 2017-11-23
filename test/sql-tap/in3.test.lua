#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(27)

--!./tcltestrunner.lua
-- 2007 November 29
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- This file tests the optimisations made in November 2007 of expressions 
-- of the following form:
--
--     <value> IN (SELECT <column> FROM <table>)
--
-- $Id: in3.test,v 1.5 2008/08/04 03:51:24 danielk1977 Exp $
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]


-- Return the number of OpenEphemeral instructions used in the
-- implementation of the sql statement passed as a an argument.
--
local function nEphemeral(sql)
    nEph = 0
    for _, op in ipairs(test:execsql("EXPLAIN "..sql.."")) do
        if (op == "OpenEphemeral")
 then
            nEph = nEph + 1
        end
    end
    return nEph
end

-- This proc works the same way as execsql, except that the number
-- of OpenEphemeral instructions used in the implementation of the
-- statement is inserted into the start of the returned list.
--
local function exec_neph(sql)
    local res = test:execsql('EXPLAIN '..sql)
    local cnt = 0
    for _, v in ipairs(res) do
        if string.find(v, 'OpenEphemeral') then
            cnt = cnt + 1
        end
    end
    res = test:execsql(sql)
    table.insert(res, 1, cnt)
    return res
    -- return X(46, "X!cmd", [=[["concat",[["nEphemeral",["sql"]]],[["execsql",["sql"]]]]]=])
end

test:do_execsql_test(
    "in3-1.1",
    [[
        CREATE TABLE t1(a PRIMARY KEY, b);
        INSERT INTO t1 VALUES(1, 2);
        INSERT INTO t1 VALUES(3, 4);
        INSERT INTO t1 VALUES(5, 6);
    ]], {
        -- <in3-1.1>
        
        -- </in3-1.1>
    })

-- All of these queries should avoid using a temp-table:
--
-- do_test in3-1.2 {
--   exec_neph { SELECT rowid FROM t1 WHERE rowid IN (SELECT rowid FROM t1); }
-- } {0 1 2 3}
test:do_test(
    "in3-1.3",
    function()
        return exec_neph(" SELECT a FROM t1 WHERE a IN (SELECT a FROM t1); ")
    end, {
        -- <in3-1.3>
        0, 1, 3, 5
        -- </in3-1.3>
    })

-- do_test in3-1.4 {
--   exec_neph { SELECT rowid FROM t1 WHERE rowid+0 IN (SELECT rowid FROM t1); }
-- } {0 1 2 3}
test:do_test(
    "in3-1.5",
    function()
        return exec_neph(" SELECT a FROM t1 WHERE a+0 IN (SELECT a FROM t1); ")
    end, {
        -- <in3-1.5>
        0, 1, 3, 5
        -- </in3-1.5>
    })

-- Because none of the sub-select queries in the following statements
-- match the pattern ("SELECT <column> FROM <table>"), the following do 
-- require a temp table.
--
-- do_test in3-1.6 {
--   exec_neph { SELECT rowid FROM t1 WHERE rowid IN (SELECT rowid+0 FROM t1); }
-- } {1 1 2 3}
test:do_test(
    "in3-1.7",
    function()
        return exec_neph(" SELECT a FROM t1 WHERE a IN (SELECT a+0 FROM t1); ")
    end, {
        -- <in3-1.7>
        1, 1, 3, 5
        -- </in3-1.7>
    })

test:do_test(
    "in3-1.8",
    function()
        return exec_neph(" SELECT a FROM t1 WHERE a IN (SELECT a FROM t1 WHERE 1); ")
    end, {
        -- <in3-1.8>
        1, 1, 3, 5
        -- </in3-1.8>
    })

test:do_test(
    "in3-1.9",
    function()
        return exec_neph(" SELECT a FROM t1 WHERE a IN (SELECT a FROM t1 GROUP BY a); ")
    end, {
        -- <in3-1.9>
        1, 1, 3, 5
        -- </in3-1.9>
    })

-- This should not use a temp-table. Even though the sub-select does
-- not exactly match the pattern "SELECT <column> FROM <table>", in
-- this case the ORDER BY is a no-op and can be ignored.
test:do_test(
    "in3-1.10",
    function()
        return exec_neph(" SELECT a FROM t1 WHERE a IN (SELECT a FROM t1 ORDER BY a); ")
    end, {
        -- <in3-1.10>
        0, 1, 3, 5
        -- </in3-1.10>
    })

-- These do use the temp-table. Adding the LIMIT clause means the 
-- ORDER BY cannot be ignored.
test:do_test(
    "in3-1.11",
    function()
        return exec_neph("SELECT a FROM t1 WHERE a IN (SELECT a FROM t1 ORDER BY a LIMIT 1)")
    end, {
        -- <in3-1.11>
        1, 1
        -- </in3-1.11>
    })

test:do_test(
    "in3-1.12",
    function()
        return exec_neph([[
    SELECT a FROM t1 WHERE a IN (SELECT a FROM t1 ORDER BY a LIMIT 1 OFFSET 1)
  ]])
    end, {
        -- <in3-1.12>
        1, 3
        -- </in3-1.12>
    })

-- Has to use a temp-table because of the compound sub-select.
--
test:do_test(
    "in3-1.13",
    function()
        return exec_neph([[
      SELECT a FROM t1 WHERE a IN (
        SELECT a FROM t1 UNION ALL SELECT a FROM t1
      )
    ]])
    end, {
        -- <in3-1.13>
        1, 1, 3, 5
        -- </in3-1.13>
    })



-- The first of these queries has to use the temp-table, because the 
-- collation sequence used for the index on "t1.a" does not match the
-- collation sequence used by the "IN" comparison. The second does not
-- require a temp-table, because the collation sequences match.
--
test:do_test(
    "in3-1.14",
    function()
        return exec_neph(" SELECT a FROM t1 WHERE a COLLATE \"unicode_ci\" IN (SELECT a FROM t1) ")
    end, {
        -- <in3-1.14>
        1, 1, 3, 5
        -- </in3-1.14>
    })

test:do_test(
    "in3-1.15",
    function()
        return exec_neph(" SELECT a FROM t1 WHERE a COLLATE binary IN (SELECT a FROM t1) ")
    end, {
        -- <in3-1.15>
        0, 1, 3, 5
        -- </in3-1.15>
    })

-- Neither of these queries require a temp-table. The collation sequence
-- makes no difference when using a rowid.
--
-- do_test in3-1.16 {
--   exec_neph {SELECT a FROM t1 WHERE a COLLATE nocase IN (SELECT rowid FROM t1)}
-- } {0 1 3}
-- do_test in3-1.17 {
--   exec_neph {SELECT a FROM t1 WHERE a COLLATE binary IN (SELECT rowid FROM t1)}
-- } {0 1 3}
-- # The following tests - in3.2.* - test a bug that was difficult to track
-- # down during development. They are not particularly well focused.
-- #
-- do_test in3-2.1 {
--   execsql {
--     DROP TABLE IF EXISTS t1;
--     CREATE TABLE t1(w int, x int, y int);
--     CREATE TABLE t2(p int, q int, r int, s int);
--   }
--   for {set i 1} {$i<=100} {incr i} {
--     set w $i
--     set x [expr {int(log($i)/log(2))}]
--     set y [expr {$i*$i + 2*$i + 1}]
--     execsql "INSERT INTO t1 VALUES($w,$x,$y)"
--   }
--   set maxy [execsql {select max(y) from t1}]
--   db eval { INSERT INTO t2 SELECT 101-w, x, $maxy+1-y, y FROM t1 }
-- } {}
-- do_test in3-2.2 {
--   execsql {
--     SELECT rowid 
--     FROM t1 
--     WHERE rowid IN (SELECT rowid FROM t1 WHERE rowid IN (1, 2));
--   }
-- } {1 2}
-- do_test in3-2.3 {
--   execsql {
--     select rowid from t1 where rowid IN (-1,2,4)
--   }
-- } {2 4}
-- do_test in3-2.4 {
--   execsql {
--     SELECT rowid FROM t1 WHERE rowid IN 
--        (select rowid from t1 where rowid IN (-1,2,4))
--   }
-- } {2 4}
---------------------------------------------------------------------------
-- This next block of tests - in3-3.* - verify that column affinity is
-- correctly handled in cases where an index might be used to optimise
-- an IN (SELECT) expression.
--
test:do_test(
    "in3-3.1",
    function()
--        X(184, "X!cmd", [=[["catch","execsql {\n    DROP TABLE t1;\n    DROP TABLE t2;\n  }"]]=])
        return test:execsql [[
            DROP TABLE IF EXISTS t1;
            DROP TABLE IF EXISTS t1;

            CREATE TABLE t1(id primary key, a BLOB, b NUMBER ,c TEXT);
            CREATE UNIQUE INDEX t1_i1 ON t1(a);        /* no affinity */
            CREATE UNIQUE INDEX t1_i2 ON t1(b);        /* numeric affinity */
            CREATE UNIQUE INDEX t1_i3 ON t1(c);        /* text affinity */

            CREATE TABLE t2(id primary key, x BLOB, y NUMBER, z TEXT);
            CREATE UNIQUE INDEX t2_i1 ON t2(x);        /* no affinity */
            CREATE UNIQUE INDEX t2_i2 ON t2(y);        /* numeric affinity */
            CREATE UNIQUE INDEX t2_i3 ON t2(z);        /* text affinity */

            INSERT INTO t1 VALUES(1, 1, 1, 1);
            INSERT INTO t2 VALUES(1, '1', '1', '1');
        ]]
    end, {
        -- <in3-3.1>
        
        -- </in3-3.1>
    })

test:do_test(
    "in3-3.2",
    function()
        -- No affinity is applied before comparing "x" and "a". Therefore
        -- the index can be used (the comparison is false, text!=number).
        return exec_neph(" SELECT x IN (SELECT a FROM t1) FROM t2 ")
    end, {
        -- <in3-3.2>
        0, 0
        -- </in3-3.2>
    })

test:do_test(
    "in3-3.3",
    function()
        -- Logically, numeric affinity is applied to both sides before 
        -- the comparison.  Therefore it is possible to use index t1_i2.
        return exec_neph(" SELECT x IN (SELECT b FROM t1) FROM t2 ")
    end, {
        -- <in3-3.3>
        0, 1
        -- </in3-3.3>
    })

test:do_test(
    "in3-3.4",
    function()
        -- No affinity is applied before the comparison takes place. Making
        -- it possible to use index t1_i3.
        return exec_neph(" SELECT x IN (SELECT c FROM t1) FROM t2 ")
    end, {
        -- <in3-3.4>
        0, 1
        -- </in3-3.4>
    })

test:do_test(
    "in3-3.5",
    function()
        -- Numeric affinity should be applied to each side before the comparison
        -- takes place. Therefore we cannot use index t1_i1, which has no affinity.
        return exec_neph(" SELECT y IN (SELECT a FROM t1) FROM t2 ")
    end, {
        -- <in3-3.5>
        1, 1
        -- </in3-3.5>
    })

test:do_test(
    "in3-3.6",
    function()
        -- Numeric affinity is applied to both sides before 
        -- the comparison.  Therefore it is possible to use index t1_i2.
        return exec_neph(" SELECT y IN (SELECT b FROM t1) FROM t2 ")
    end, {
        -- <in3-3.6>
        0, 1
        -- </in3-3.6>
    })

test:do_test(
    "in3-3.7",
    function()
        -- Numeric affinity is applied before the comparison takes place. 
        -- Making it impossible to use index t1_i3.
        return exec_neph(" SELECT y IN (SELECT c FROM t1) FROM t2 ")
    end, {
        -- <in3-3.7>
        1, 1
        -- </in3-3.7>
    })

-----------------------------------------------------------------------
--
-- Test using a multi-column index.
--
test:do_test(
    "in3-4.1",
    function()
        test:execsql [[
            CREATE TABLE t3(a PRIMARY KEY, b, c);
            CREATE UNIQUE INDEX t3_i ON t3(b, a);
        ]]
        return test:execsql [[
            INSERT INTO t3 VALUES(1, 'numeric', 2);
            INSERT INTO t3 VALUES(2, 'text', 2);
            INSERT INTO t3 VALUES(3, 'real', 2);
            INSERT INTO t3 VALUES(4, 'none', 2);
        ]]
    end, {
        -- <in3-4.1>
        
        -- </in3-4.1>
    })

test:do_test(
    "in3-4.2",
    function()
        return exec_neph(" SELECT 'text' IN (SELECT b FROM t3) ")
    end, {
        -- <in3-4.2>
        0, 1
        -- </in3-4.2>
    })

test:do_test(
    "in3-4.3",
    function()
        return exec_neph(" SELECT 'TEXT' COLLATE \"unicode_ci\" IN (SELECT b FROM t3) ")
    end, {
        -- <in3-4.3>
        1, 1
        -- </in3-4.3>
    })

test:do_test(
    "in3-4.4",
    function()
        -- A temp table must be used because t3_i.b is not guaranteed to be unique.
        return exec_neph(" SELECT b FROM t3 WHERE b IN (SELECT b FROM t3) ")
    end, {
        -- <in3-4.4>
        1, "none", "numeric", "real", "text"
        -- </in3-4.4>
    })

test:do_test(
    "in3-4.5",
    function()
        test:execsql " CREATE UNIQUE INDEX t3_i2 ON t3(b) "
        return exec_neph(" SELECT b FROM t3 WHERE b IN (SELECT b FROM t3) ")
    end, {
        -- <in3-4.5>
        0, "none", "numeric", "real", "text"
        -- </in3-4.5>
    })

test:do_execsql_test(
    "in3-4.6",
    [[
        DROP INDEX t3_i2 ON t3
    ]], {
        -- <in3-4.6>
        
        -- </in3-4.6>
    })

-- The following two test cases verify that ticket #2991 has been fixed.
--
test:do_execsql_test(
    "in3-5.1",
    [[
        CREATE TABLE Folders(
          folderid INTEGER PRIMARY KEY, 
          parentid INTEGER, 
          rootid INTEGER, 
          path VARCHAR(255)
        );
    ]], {
        -- <in3-5.1>
        
        -- </in3-5.1>
    })

test:do_catchsql_test(
    "in3-5.2",
    [[
        DELETE FROM Folders WHERE folderid IN
        (SELECT folderid FROM Folder WHERE path LIKE 'C:\MP3\Albums\' || '%');
    ]], {
        -- <in3-5.2>
        1, "no such table: FOLDER"
        -- </in3-5.2>
    })

test:finish_test()

