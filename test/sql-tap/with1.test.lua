#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(64)

--!./tcltestrunner.lua
-- 2014 January 11
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
-- focus of this file is testing the WITH clause.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- if X(0, "X!capable", [["!cte"]]) then
--   test:finish_test()
--  return
-- end

test:do_execsql_test(1.0, [[
  CREATE TABLE t1(x INTEGER UNIQUE, y INTEGER, z INTEGER PRIMARY KEY);
  WITH x(a) AS ( SELECT * FROM t1) SELECT 10
]], {
  -- <1.0>
  10
  -- </1.0>
})

test:do_execsql_test(1.1, [[
  SELECT * FROM ( WITH x AS ( SELECT * FROM t1) SELECT 10 );
]], {
  -- <1.1>
  10
  -- </1.1>
})

test:do_execsql_test(1.2, [[
  WITH x(a) AS ( SELECT * FROM t1) INSERT INTO t1 VALUES(1,1,2);
]], {
  -- <1.2>

  -- </1.2>
})

test:do_execsql_test(1.3, [[
  WITH x(a) AS ( SELECT * FROM t1) DELETE FROM t1;
]], {
  -- <1.3>

  -- </1.3>
})

test:do_execsql_test(1.4, [[
  WITH x(a) AS ( SELECT * FROM t1) UPDATE t1 SET x = y;
]], {
  -- <1.4>

  -- </1.4>
})

----------------------------------------------------------------------------
test:do_execsql_test(2.1, [[
  DROP TABLE IF EXISTS t1;
  CREATE TABLE t1(x INT PRIMARY KEY);
  INSERT INTO t1 VALUES(1);
  INSERT INTO t1 VALUES(2);
  WITH tmp AS ( SELECT * FROM t1 ) SELECT x FROM tmp;
]], {
  -- <2.1>
  1, 2
  -- </2.1>
})

test:do_execsql_test(2.2, [[
  WITH tmp(a) AS ( SELECT * FROM t1 ) SELECT a FROM tmp;
]], {
  -- <2.2>
  1, 2
  -- </2.2>
})

test:do_execsql_test(2.3, [[
  SELECT * FROM (
    WITH tmp(a) AS ( SELECT * FROM t1 ) SELECT a FROM tmp
  );
]], {
  -- <2.3>
  1, 2
  -- </2.3>
})

test:do_execsql_test(2.4, [[
  WITH tmp1(a) AS ( SELECT * FROM t1 ),
       tmp2(x) AS ( SELECT * FROM tmp1)
  SELECT * FROM tmp2;
]], {
  -- <2.4>
  1, 2
  -- </2.4>
})

test:do_execsql_test(2.5, [[
  WITH tmp2(x) AS ( SELECT * FROM tmp1),
       tmp1(a) AS ( SELECT * FROM t1 )
  SELECT * FROM tmp2;
]], {
  -- <2.5>
  1, 2
  -- </2.5>
})

---------------------------------------------------------------------------
test:do_catchsql_test(3.1, [[
  WITH tmp2(x) AS ( SELECT * FROM tmp1 ),
       tmp1(a) AS ( SELECT * FROM tmp2 )
  SELECT * FROM tmp1;
]], {
  -- <3.1>
  1, "circular reference: TMP1"
  -- </3.1>
})

test:do_catchsql_test(3.2, [[
  CREATE TABLE t2(x INTEGER PRIMARY KEY);
  WITH tmp(a) AS (SELECT * FROM t1),
       tmp(a) AS (SELECT * FROM t1)
  SELECT * FROM tmp;
]], {
  -- <3.2>
  1, "Ambiguous table name in WITH query: TMP"
  -- </3.2>
})

test:do_execsql_test(3.3, [[
  CREATE TABLE t3(x TEXT PRIMARY KEY);
  CREATE TABLE t4(x TEXT PRIMARY KEY);

  INSERT INTO t3 VALUES('T3');
  INSERT INTO t4 VALUES('T4');

  WITH t3(a) AS (SELECT * FROM t4)
  SELECT * FROM t3;
]], {
  -- <3.3>
  "T4"
  -- </3.3>
})

test:do_execsql_test(3.4, [[
  WITH tmp  AS ( SELECT * FROM t3 ),
       tmp2 AS ( WITH tmp AS ( SELECT * FROM t4 ) SELECT * FROM tmp )
  SELECT * FROM tmp2;
]], {
  -- <3.4>
  "T4"
  -- </3.4>
})

test:do_execsql_test(3.5, [[
  WITH tmp  AS ( SELECT * FROM t3 ),
       tmp2 AS ( WITH xxxx AS ( SELECT * FROM t4 ) SELECT * FROM tmp )
  SELECT * FROM tmp2;
]], {
  -- <3.5>
  "T3"
  -- </3.5>
})

test:do_catchsql_test(3.6, [[
  WITH tmp AS ( SELECT * FROM t3 ),
  SELECT * FROM tmp;
]], {
  -- <3.6>
  1, [[At line 2 at or near position 3: keyword 'SELECT' is reserved. Please use double quotes if 'SELECT' is an identifier.]]
  -- </3.6>
})

---------------------------------------------------------------------------
test:do_execsql_test(4.1, [[
  DROP TABLE IF EXISTS t1;
  CREATE TABLE t1(x INT UNIQUE, z INT PRIMARY KEY AUTOINCREMENT);
  INSERT INTO t1(x) VALUES(1);
  INSERT INTO t1(x) VALUES(2);
  INSERT INTO t1(x) VALUES(3);
  INSERT INTO t1(x) VALUES(4);

  WITH dset AS ( SELECT 2 UNION ALL SELECT 4 )
  DELETE FROM t1 WHERE x IN dset;
  SELECT (x) FROM t1;
]], {
  -- <4.1>
  1, 3
  -- </4.1>
})

test:do_execsql_test(4.2, [[
  WITH iset AS ( SELECT 2 UNION ALL SELECT 4 )
  INSERT INTO t1(x) SELECT * FROM iset;
  SELECT x FROM t1;
]], {
  -- <4.2>
  1, 3, 2, 4
  -- </4.2>
})

test:do_execsql_test(4.3, [[
  WITH uset(a, b) AS ( SELECT 2, 8 UNION ALL SELECT 4, 9 )
  UPDATE t1 SET x = COALESCE( (SELECT b FROM uset WHERE a=x), x );
  SELECT x FROM t1;
]], {
  -- <4.3>
  1, 3, 8, 9
  -- </4.3>
})

---------------------------------------------------------------------------
--
test:do_execsql_test(5.1, [[
  WITH i(x) AS ( VALUES(1) UNION ALL SELECT x+1 FROM i)
  SELECT x FROM i LIMIT 10;
]], {
  -- <5.1>
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10
  -- </5.1>
})

test:do_catchsql_test(5.2, [[
  WITH i(x) AS ( VALUES(1) UNION ALL SELECT x+1 FROM i ORDER BY 1)
  SELECT x FROM i LIMIT 10;
]], {
  -- <5.2>
  0, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}
  -- </5.2>
})

test:do_execsql_test("5.2.1", [[
  CREATE TABLE edge(xfrom INT, xto INT, seq INT, PRIMARY KEY(xfrom, xto));
  INSERT INTO edge VALUES(0, 1, 10);
  INSERT INTO edge VALUES(1, 2, 20);
  INSERT INTO edge VALUES(0, 3, 30);
  INSERT INTO edge VALUES(2, 4, 40);
  INSERT INTO edge VALUES(3, 4, 40);
  INSERT INTO edge VALUES(2, 5, 50);
  INSERT INTO edge VALUES(3, 6, 60);
  INSERT INTO edge VALUES(5, 7, 70);
  INSERT INTO edge VALUES(3, 7, 70);
  INSERT INTO edge VALUES(4, 8, 80);
  INSERT INTO edge VALUES(7, 8, 80);
  INSERT INTO edge VALUES(8, 9, 90);

  WITH RECURSIVE
    ancest(id, mtime) AS
      (VALUES(0, 0)
       UNION
       SELECT edge.xto, edge.seq FROM edge, ancest
        WHERE edge.xfrom=ancest.id
        ORDER BY 2
      )
  SELECT * FROM ancest;
]], {
  -- <5.2.1>
  0, 0, 1, 10, 2, 20, 3, 30, 4, 40, 5, 50, 6, 60, 7, 70, 8, 80, 9, 90
  -- </5.2.1>
})

test:do_execsql_test("5.2.2", [[
  WITH RECURSIVE
    ancest(id, mtime) AS
      (VALUES(0, 0)
       UNION ALL
       SELECT edge.xto, edge.seq FROM edge, ancest
        WHERE edge.xfrom=ancest.id
        ORDER BY 2
      )
  SELECT * FROM ancest;
]], {
  -- <5.2.2>
  0, 0, 1, 10, 2, 20, 3, 30, 4, 40, 4, 40, 5, 50, 6, 60, 7, 70, 7, 70, 8, 80, 8, 80, 8, 80, 8, 80, 9, 90, 9, 90, 9, 90, 9, 90
  -- </5.2.2>
})

test:do_execsql_test("5.2.3", [[
  WITH RECURSIVE
    ancest(id, mtime) AS
      (VALUES(0, 0)
       UNION ALL
       SELECT edge.xto, edge.seq FROM edge, ancest
        WHERE edge.xfrom=ancest.id
        ORDER BY 2 LIMIT 4 OFFSET 2
      )
  SELECT * FROM ancest;
]], {
  -- <5.2.3>
  2, 20, 3, 30, 4, 40, 4, 40
  -- </5.2.3>
})

test:do_catchsql_test(5.3, [[
  WITH i(x) AS ( VALUES(1) UNION ALL SELECT x+1 FROM i LIMIT 5)
  SELECT x FROM i;
]], {
  -- <5.3>
  0, {1, 2, 3, 4, 5}
  -- </5.3>
})

test:do_execsql_test(5.4, [[
  WITH i(x) AS ( VALUES(1) UNION ALL SELECT (x+1)%10 FROM i)
  SELECT x FROM i LIMIT 20;
]], {
  -- <5.4>
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0
  -- </5.4>
})

test:do_execsql_test(5.5, [[
  WITH i(x) AS ( VALUES(1) UNION SELECT (x+1)%10 FROM i)
  SELECT x FROM i LIMIT 20;
]], {
  -- <5.5>
  1, 2, 3, 4, 5, 6, 7, 8, 9, 0
  -- </5.5>
})

test:do_catchsql_test("5.6.1", [[
  WITH i(x, y) AS ( VALUES(1) )
  SELECT * FROM i;
]], {
  -- <5.6.1>
  1, "table I has 1 values for 2 columns"
  -- </5.6.1>
})

test:do_catchsql_test("5.6.2", [[
  WITH i(x) AS ( VALUES(1,2) )
  SELECT * FROM i;
]], {
  -- <5.6.2>
  1, "table I has 2 values for 1 columns"
  -- </5.6.2>
})

test:do_catchsql_test("5.6.3", [[
  CREATE TABLE t5(a  INT PRIMARY KEY, b INT );
  WITH i(x) AS ( SELECT * FROM t5 )
  SELECT * FROM i;
]], {
  -- <5.6.3>
  1, "table I has 2 values for 1 columns"
  -- </5.6.3>
})

test:do_catchsql_test("5.6.4", [[
  WITH i(x) AS ( SELECT 1, 2 UNION ALL SELECT 1 )
  SELECT * FROM i;
]], {
  -- <5.6.4>
  1, "table I has 2 values for 1 columns"
  -- </5.6.4>
})

test:do_catchsql_test("5.6.5", [[
  WITH i(x) AS ( SELECT 1 UNION ALL SELECT 1, 2 )
  SELECT * FROM i;
]], {
  -- <5.6.5>
  1, "SELECTs to the left and right of UNION ALL do not have the same number of result columns"
  -- </5.6.5>
})

test:do_catchsql_test("5.6.6", [[
  WITH i(x) AS ( SELECT 1 UNION ALL SELECT x+1, x*2 FROM i )
  SELECT * FROM i;
]], {
  -- <5.6.6>
  1, "SELECTs to the left and right of UNION ALL do not have the same number of result columns"
  -- </5.6.6>
})

test:do_catchsql_test("5.6.7", [[
  WITH i(x) AS ( SELECT 1, 2 UNION SELECT x+1 FROM i )
  SELECT * FROM i;
]], {
  -- <5.6.7>
  1, "table I has 2 values for 1 columns"
  -- </5.6.7>
})

---------------------------------------------------------------------------
--
test:do_execsql_test(6.1, [[
  CREATE TABLE f(
      id INTEGER PRIMARY KEY, parentid  INT REFERENCES f, name TEXT
  );

  INSERT INTO f VALUES(0, NULL, '');
  INSERT INTO f VALUES(1, 0, 'bin');
  INSERT INTO f VALUES(2, 1, 'true');
  INSERT INTO f VALUES(3, 1, 'false');
  INSERT INTO f VALUES(4, 1, 'ls');
  INSERT INTO f VALUES(5, 1, 'grep');
  INSERT INTO f VALUES(6, 0, 'etc');
  INSERT INTO f VALUES(7, 6, 'rc.d');
  INSERT INTO f VALUES(8, 7, 'rc.apache');
  INSERT INTO f VALUES(9, 7, 'rc.samba');
  INSERT INTO f VALUES(10, 0, 'home');
  INSERT INTO f VALUES(11, 10, 'dan');
  INSERT INTO f VALUES(12, 11, 'public_html');
  INSERT INTO f VALUES(13, 12, 'index.html');
  INSERT INTO f VALUES(14, 13, 'logo.gif');
]])

test:do_execsql_test(6.2, [[
  WITH flat(fid, fpath) AS (
    SELECT id, '' FROM f WHERE parentid IS NULL
    UNION ALL
    SELECT id, fpath || '/' || name FROM f, flat WHERE parentid=fid
  )
  SELECT fpath FROM flat WHERE fpath!='' ORDER BY 1;
]], {
  -- <6.2>
  "/bin", "/bin/false", "/bin/grep", "/bin/ls", "/bin/true", "/etc", "/etc/rc.d", "/etc/rc.d/rc.apache", "/etc/rc.d/rc.samba", "/home", "/home/dan", "/home/dan/public_html", "/home/dan/public_html/index.html", "/home/dan/public_html/index.html/logo.gif"
  -- </6.2>
})

test:do_execsql_test(6.3, [[
  WITH flat(fid, fpath) AS (
    SELECT id, '' FROM f WHERE parentid IS NULL
    UNION ALL
    SELECT id, fpath || '/' || name FROM f, flat WHERE parentid=fid
  )
  SELECT count(*) FROM flat;
]], {
  -- <6.3>
  15
  -- </6.3>
})

test:do_execsql_test(6.4, [[
  WITH x(i) AS (
    SELECT 1
    UNION ALL
    SELECT i+1 FROM x WHERE i<10
  )
  SELECT count(*) FROM x
]], {
  -- <6.4>
  10
  -- </6.4>
})

---------------------------------------------------------------------------
test:do_execsql_test(7.1, [[
  CREATE TABLE tree(i  INT PRIMARY KEY, p INT );
  INSERT INTO tree VALUES(1, NULL);
  INSERT INTO tree VALUES(2, 1);
  INSERT INTO tree VALUES(3, 1);
  INSERT INTO tree VALUES(4, 2);
  INSERT INTO tree VALUES(5, 4);
]])

test:do_execsql_test(7.2, [[
  WITH t(id, path) AS (
    SELECT i, '' FROM tree WHERE p IS NULL
    UNION ALL
    SELECT i, path || '/' || CAST(i as TEXT) FROM tree, t WHERE p = id
  )
  SELECT path FROM t;
]], {
  -- <7.2>
  "", "/2", "/3", "/2/4", "/2/4/5"
  -- </7.2>
})

test:do_execsql_test(7.3, [[
  WITH t(id) AS (
    VALUES(2)
    UNION ALL
    SELECT i FROM tree, t WHERE p = id
  )
  SELECT id FROM t;
]], {
  -- <7.3>
  2, 4, 5
  -- </7.3>
})

test:do_catchsql_test(7.4, [[
  WITH t(id) AS (
    VALUES(2)
    UNION ALL
    SELECT i FROM tree WHERE p IN (SELECT id FROM t)
  )
  SELECT id FROM t;
]], {
  -- <7.4>
  1, "recursive reference in a subquery: T"
  -- </7.4>
})

test:do_catchsql_test(7.5, [[
  WITH t(id) AS (
    VALUES(2)
    UNION ALL
    SELECT i FROM tree, t WHERE p = id AND p IN (SELECT id FROM t)
  )
  SELECT id FROM t;
]], {
  -- <7.5>
  1, "multiple recursive references: T"
  -- </7.5>
})

test:do_catchsql_test(7.6, [[
  WITH t(id) AS (
    SELECT i FROM tree WHERE 2 IN (SELECT id FROM t)
    UNION ALL
    SELECT i FROM tree, t WHERE p = id
  )
  SELECT id FROM t;
]], {
  -- <7.6>
  1, "circular reference: T"
  -- </7.6>
})

-- Compute the mandelbrot set using a recursive query
--
test:do_execsql_test("8.1-mandelbrot", [[
  WITH RECURSIVE
    xaxis(x) AS (VALUES(-2e0) UNION ALL SELECT x + 0.05e0 FROM xaxis WHERE x < 1.2e0),
    yaxis(y) AS (VALUES(-1e0) UNION ALL SELECT y + 0.1e0 FROM yaxis WHERE y < 1.0e0),
    m(iter, cx, cy, x, y) AS (
      SELECT 0, x, y, 0e0, 0e0 FROM xaxis, yaxis
      UNION ALL
      SELECT iter+1, cx, cy, x*x-y*y + cx, 2.0*x*y + cy FROM m
       WHERE (x*x + y*y) < 4.0 AND iter<28
    ),
    m2(iter, cx, cy) AS (
      SELECT max(iter), cx, cy FROM m GROUP BY cx, cy
    ),
    a(t) AS (
      SELECT group_concat( substr(' .+*#', 1+LEAST(iter/7,4), 1), '')
      FROM m2 GROUP BY cy
    )
  SELECT group_concat(CAST(TRIM(TRAILING FROM t) AS VARBINARY),x'0a') FROM a;
]], {
  -- <8.1-mandelbrot>
  require('varbinary').new([[                                    ....#
                                   ..#*..
                                 ..+####+.
                            .......+####....   +
                           ..##+*##########+.++++
                          .+.##################+.
              .............+###################+.+
              ..++..#.....*#####################+.
             ...+#######++#######################.
          ....+*################################.
 #############################################...
          ....+*################################.
             ...+#######++#######################.
              ..++..#.....*#####################+.
              .............+###################+.+
                          .+.##################+.
                           ..##+*##########+.++++
                            .......+####....   +
                                 ..+####+.
                                   ..#*..
                                    ....#
                                    +.]])
  -- </8.1-mandelbrot>
})

-- Solve a sudoku puzzle using a recursive query
--
test:do_execsql_test("8.2-soduko", [[
  WITH RECURSIVE
    input(sud) AS (
      VALUES('53..7....6..195....98....6.8...6...34..8.3..17...2...6.6....28....419..5....8..79')
    ),

    /* A table filled with digits 1..9, inclusive. */
    digits(z, lp) AS (
      VALUES('1', 1)
      UNION ALL SELECT
      CAST(lp+1 AS TEXT), lp+1 FROM digits WHERE lp<9
    ),

    /* The tricky bit. */
    x(s, ind) AS (
      SELECT sud, position('.', sud) FROM input
      UNION ALL
      SELECT
        substr(s, 1, ind-1) || z || substr(s, ind+1),
        position('.', substr(s, 1, ind-1) || z || substr(s, ind+1))
       FROM x, digits AS z
      WHERE ind>0
        AND NOT EXISTS (
              SELECT 1
                FROM digits AS lp
               WHERE z.z = substr(s, ((ind-1)/9)*9 + lp, 1)
                  OR z.z = substr(s, ((ind-1)%9) + (lp-1)*9 + 1, 1)
                  OR z.z = substr(s, (((ind-1)/3) % 3) * 3
                          + ((ind-1)/27) * 27 + lp
                          + ((lp-1) / 3) * 6, 1) LIMIT 1
           )
    )
  SELECT s FROM x WHERE ind=0;
]], {
  -- <8.2-soduko>
  '534678912672195348198342567859761423426853791713924856961537284287419635345286179'
  -- </8.2-soduko>
})

----------------------------------------------------------------------------
-- Some tests that use LIMIT and OFFSET in the definition of recursive CTEs.
--
-- I = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 }
local function limit_test(tn, iLimit, iOffset)
    local last = 20 - 1
    local result = {}
    -- if X(0, "X!expr01", [["$iOffset < 0"]]) then
    if iOffset < 0 then
        iOffset = 0
    end
    -- if X(0, "X!expr01", [["$iLimit < 0 "]]) then
    if iLimit < 0 then
        assert(iLimit < 0) -- added to pass luacheck
        -- result = X(467, "X!cmd", [=[["lrange",["::I"],["iOffset"],"end"]]=])
        -- last is 20
    else
        -- result = X(467, "X!cmd", [=[["lrange",["::I"],["iOffset"],[["expr",[["iLimit"],"+",["iOffset"],"-1"]]]]]=])
        last = iLimit + iOffset - 1
        if last > 20 then last = 20 - 1 end
    end

    for i = iOffset, last do
        result[i - iOffset + 1] = i + 1
    end

    -- X(469, "X!cmd", [=[["uplevel",[["list","do_execsql_test",["tn"],["\n    WITH ii(a) AS (\n      VALUES(1)\n      UNION ALL \n      SELECT a+1 FROM ii WHERE a<20 \n      LIMIT ",["iLimit"]," OFFSET ",["iOffset"],"\n    )\n    SELECT * FROM ii\n  "],["result"]]]]]=])
    test:do_execsql_test(tn, string.format([[
      WITH ii(a) AS (
        VALUES(1)
        UNION ALL
        SELECT a+1 FROM ii WHERE a<20
        LIMIT %s OFFSET %s
      )
      SELECT * FROM ii ]], iLimit, iOffset),
                         result)
end

limit_test(9.1, 20, 0)
limit_test(9.2, 0, 0)
limit_test(9.3, 19, 1)
limit_test(9.4, 20, -1)
limit_test(9.5, 5, 5)
limit_test(9.6, 0, -1)
limit_test(9.7, 40, -1)
-- #--------------------------------------------------------------------------
-- # Test the ORDER BY clause on recursive tables.
-- #
-- do_execsql_test 10.1 {
--   DROP TABLE IF EXISTS tree;
--   CREATE TABLE tree(id INTEGER PRIMARY KEY, parentid INT , payload INT );
-- }
-- proc insert_into_tree {L} {
--   db eval { DELETE FROM tree }
--   foreach key $L {
--     unset -nocomplain parentid
--     foreach seg [split $key /] {
--       if {$seg==""} continue
--       set id [db one {
--         SELECT id FROM tree WHERE parentid IS $parentid AND payload=$seg
--       }]
--       if {$id==""} {
--         db eval { INSERT INTO tree VALUES(NULL, $parentid, $seg) }
--         set parentid [db last_insert_rowid]
--       } else {
--         set parentid $id
--       }
--     }
--   }
-- }
-- insert_into_tree {
--   /a/a/a
--   /a/b/c
--   /a/b/c/d
--   /a/b/d
-- }
-- do_execsql_test 10.2 {
--   WITH flat(fid, p) AS (
--     SELECT id, '/' || payload FROM tree WHERE parentid IS NULL
--     UNION ALL
--     SELECT id, p || '/' || payload FROM flat, tree WHERE parentid=fid
--   )
--   SELECT p FROM flat ORDER BY p;
-- } {
--   /a /a/a /a/a/a
--      /a/b /a/b/c /a/b/c/d
--           /a/b/d
-- }
-- # Scan the tree-structure currently stored in table tree. Return a list
-- # of nodes visited.
-- #
-- proc scan_tree {bDepthFirst bReverse} {
--   set order "ORDER BY "
--   if {$bDepthFirst==0} { append order "2 ASC," }
--   if {$bReverse==0} {
--     append order " 3 ASC"
--   } else {
--     append order " 3 DESC"
--   }
--   db eval "
--     WITH flat(fid, depth, p) AS (
--         SELECT id, 1, '/' || payload FROM tree WHERE parentid IS NULL
--         UNION ALL
--         SELECT id, depth+1, p||'/'||payload FROM flat, tree WHERE parentid=fid
--         $order
--     )
--     SELECT p FROM flat;
--   "
-- }
-- insert_into_tree {
--   /a/b
--   /a/b/c
--   /a/d
--   /a/d/e
--   /a/d/f
--   /g/h
-- }
-- # Breadth first, siblings in ascending order.
-- #
-- do_test 10.3 {
--   scan_tree 0 0
-- } [list {*}{
--   /a /g
--   /a/b /a/d /g/h
--   /a/b/c /a/d/e /a/d/f
-- }]
-- # Depth first, siblings in ascending order.
-- #
-- do_test 10.4 {
--   scan_tree 1 0
-- } [list {*}{
--   /a /a/b /a/b/c
--      /a/d /a/d/e
--           /a/d/f
--   /g /g/h
-- }]
-- # Breadth first, siblings in descending order.
-- #
-- do_test 10.5 {
--   scan_tree 0 1
-- } [list {*}{
--   /g /a
--   /g/h /a/d /a/b
--   /a/d/f /a/d/e /a/b/c
-- }]
-- # Depth first, siblings in ascending order.
-- #
-- do_test 10.6 {
--   scan_tree 1 1
-- } [list {*}{
--   /g /g/h
--   /a /a/d /a/d/f
--           /a/d/e
--      /a/b /a/b/c
-- }]
-- Test name resolution in ORDER BY clauses.
--
test:do_catchsql_test("10.7.1", [[
  WITH t(a) AS (
    SELECT 1 AS b UNION ALL SELECT a+1 AS c FROM t WHERE a<5 ORDER BY a
  )
  SELECT * FROM t
]], {
  -- <10.7.1>
  1, "Error at ORDER BY in place 1: term does not match any column in the result set"
  -- </10.7.1>
})

test:do_execsql_test("10.7.2", [[
  WITH t(a) AS (
    SELECT 1 AS b UNION ALL SELECT a+1 AS c FROM t WHERE a<5 ORDER BY b
  )
  SELECT * FROM t
]], {
  -- <10.7.2>
  1, 2, 3, 4, 5
  -- </10.7.2>
})

-- # Test COLLATE clauses attached to ORDER BY.
-- #
-- insert_into_tree {
--   /a/b
--   /a/C
--   /a/d
--   /B/e
--   /B/F
--   /B/g
--   /c/h
--   /c/I
--   /c/j
-- }
-- do_execsql_test 10.8.1 {
--   WITH flat(fid, depth, p) AS (
--     SELECT id, 1, '/' || payload FROM tree WHERE parentid IS NULL
--     UNION ALL
--     SELECT id, depth+1, p||'/'||payload FROM flat, tree WHERE parentid=fid
--     ORDER BY 2, 3 COLLATE nocase
--   )
--   SELECT p FROM flat;
-- } {
--   /a /B /c
--   /a/b /a/C /a/d /B/e /B/F /B/g /c/h /c/I /c/j
-- }
-- do_execsql_test 10.8.2 {
--   WITH flat(fid, depth, p) AS (
--       SELECT id, 1, ('/' || payload) COLLATE nocase
--       FROM tree WHERE parentid IS NULL
--     UNION ALL
--       SELECT id, depth+1, (p||'/'||payload)
--       FROM flat, tree WHERE parentid=fid
--     ORDER BY 2, 3
--   )
--   SELECT p FROM flat;
-- } {
--   /a /B /c
--   /a/b /a/C /a/d /B/e /B/F /B/g /c/h /c/I /c/j
-- }
-- do_execsql_test 10.8.3 {
--   WITH flat(fid, depth, p) AS (
--       SELECT id, 1, ('/' || payload)
--       FROM tree WHERE parentid IS NULL
--     UNION ALL
--       SELECT id, depth+1, (p||'/'||payload) COLLATE nocase
--       FROM flat, tree WHERE parentid=fid
--     ORDER BY 2, 3
--   )
--   SELECT p FROM flat;
-- } {
--   /a /B /c
--   /a/b /a/C /a/d /B/e /B/F /B/g /c/h /c/I /c/j
-- }
test:do_execsql_test("10.8.4.1", [[
  CREATE TABLE tst(a  TEXT PRIMARY KEY,b TEXT );
  INSERT INTO tst VALUES('a', 'A');
  INSERT INTO tst VALUES('b', 'B');
  INSERT INTO tst VALUES('c', 'C');
  SELECT a COLLATE "unicode_ci" FROM tst UNION ALL SELECT b FROM tst ORDER BY 1;
]], {
  -- <10.8.4.1>
  "a", "A", "b", "B", "c", "C"
  -- </10.8.4.1>
})

-- MUST_WORK_TEST
test:do_execsql_test("10.8.4.2", [[
  SELECT a FROM tst UNION ALL SELECT b COLLATE "unicode_ci" FROM tst ORDER BY 1;
]], {
  -- <10.8.4.2>
  "a", "A", "b", "B", "c", "C"
  -- </10.8.4.2>
})

test:do_execsql_test("10.8.4.3", [[
  SELECT a||'' FROM tst UNION ALL SELECT b COLLATE "unicode_ci" FROM tst ORDER BY 1;
]], {
  -- <10.8.4.3>
  "a", "A", "b", "B", "c", "C"
  -- </10.8.4.3>
})

-- MUST_WORK_TEST
-- # Test cases to illustrate on the ORDER BY clause on a recursive query can be
-- # used to control depth-first versus breath-first search in a tree.
-- #
-- do_execsql_test 11.1 {
--   CREATE TABLE org(
--     name TEXT PRIMARY KEY,
--     boss TEXT REFERENCES org
--   );
--   INSERT INTO org VALUES('Alice',NULL);
--   INSERT INTO org VALUES('Bob','Alice');
--   INSERT INTO org VALUES('Cindy','Alice');
--   INSERT INTO org VALUES('Dave','Bob');
--   INSERT INTO org VALUES('Emma','Bob');
--   INSERT INTO org VALUES('Fred','Cindy');
--   INSERT INTO org VALUES('Gail','Cindy');
--   INSERT INTO org VALUES('Harry','Dave');
--   INSERT INTO org VALUES('Ingrid','Dave');
--   INSERT INTO org VALUES('Jim','Emma');
--   INSERT INTO org VALUES('Kate','Emma');
--   INSERT INTO org VALUES('Lanny','Fred');
--   INSERT INTO org VALUES('Mary','Fred');
--   INSERT INTO org VALUES('Noland','Gail');
--   INSERT INTO org VALUES('Olivia','Gail');
--   -- The above are all under Alice.  Add a few more records for people
--   -- not in Alice's group, just to prove that they won't be selected.
--   INSERT INTO org VALUES('Xaviar',NULL);
--   INSERT INTO org VALUES('Xia','Xaviar');
--   INSERT INTO org VALUES('Xerxes','Xaviar');
--   INSERT INTO org VALUES('Xena','Xia');
--   -- Find all members of Alice's group, breath-first order
--   WITH RECURSIVE
--     under_alice(name,level) AS (
--        VALUES('Alice','0')
--        UNION ALL
--        SELECT org.name, under_alice.level+1
--          FROM org, under_alice
--         WHERE org.boss=under_alice.name
--         ORDER BY 2
--     )
--   SELECT group_concat(substr('...............',1,level*3) || name,x'0a')
--     FROM under_alice;
-- } {{Alice
-- ...Bob
-- ...Cindy
-- ......Dave
-- ......Emma
-- ......Fred
-- ......Gail
-- .........Harry
-- .........Ingrid
-- .........Jim
-- .........Kate
-- .........Lanny
-- .........Mary
-- .........Noland
-- .........Olivia}}
-- # The previous query used "ORDER BY level" to yield a breath-first search.
-- # Change that to "ORDER BY level DESC" for a depth-first search.
-- #
-- do_execsql_test 11.2 {
--   WITH RECURSIVE
--     under_alice(name,level) AS (
--        VALUES('Alice','0')
--        UNION ALL
--        SELECT org.name, under_alice.level+1
--          FROM org, under_alice
--         WHERE org.boss=under_alice.name
--         ORDER BY 2 DESC
--     )
--   SELECT group_concat(substr('...............',1,level*3) || name,x'0a')
--     FROM under_alice;
-- } {{Alice
-- ...Bob
-- ......Dave
-- .........Harry
-- .........Ingrid
-- ......Emma
-- .........Jim
-- .........Kate
-- ...Cindy
-- ......Fred
-- .........Lanny
-- .........Mary
-- ......Gail
-- .........Noland
-- .........Olivia}}
-- # Without an ORDER BY clause, the recursive query should use a FIFO,
-- # resulting in a breath-first search.
-- #
-- do_execsql_test 11.3 {
--   WITH RECURSIVE
--     under_alice(name,level) AS (
--        VALUES('Alice','0')
--        UNION ALL
--        SELECT org.name, under_alice.level+1
--          FROM org, under_alice
--         WHERE org.boss=under_alice.name
--     )
--   SELECT group_concat(substr('...............',1,level*3) || name,x'0a')
--     FROM under_alice;
-- } {{Alice
-- ...Bob
-- ...Cindy
-- ......Dave
-- ......Emma
-- ......Fred
-- ......Gail
-- .........Harry
-- .........Ingrid
-- .........Jim
-- .........Kate
-- .........Lanny
-- .........Mary
-- .........Noland
-- .........Olivia}}
----------------------------------------------------------------------------
-- Ticket [31a19d11b97088296ac104aaff113a9790394927] (2014-02-09)
-- Name resolution issue with compound SELECTs and Common Table Expressions
--
test:do_execsql_test(12.1, [[
  WITH RECURSIVE
    t1(x) AS (VALUES(2) UNION ALL SELECT x+2 FROM t1 WHERE x<20),
    t2(y) AS (VALUES(3) UNION ALL SELECT y+3 FROM t2 WHERE y<20)
  SELECT x FROM t1 EXCEPT SELECT y FROM t2 ORDER BY 1;
]], {
  -- <12.1>
  2, 4, 8, 10, 14, 16, 20
  -- </12.1>
})

-- 2015-03-21
-- Column wildcards on the LHS of a recursive table expression
--
test:do_catchsql_test(13.1, [[
  WITH RECURSIVE c(i) AS (SELECT * UNION ALL SELECT i+1 FROM c WHERE i<10)
  SELECT i FROM c;
]], {
  -- <13.1>
  1, "Failed to expand '*' in SELECT statement without FROM clause"
  -- </13.1>
})

test:do_catchsql_test(13.2, [[
  WITH RECURSIVE c(i) AS (SELECT 5,* UNION ALL SELECT i+1 FROM c WHERE i<10)
  SELECT i FROM c;
]], {
  -- <13.2>
  1, "Failed to expand '*' in SELECT statement without FROM clause"
  -- </13.2>
})

test:do_catchsql_test(13.3, [[
  WITH RECURSIVE c(i,j) AS (SELECT 5,* UNION ALL SELECT i+1,11 FROM c WHERE i<10)
  SELECT i FROM c;
]], {
  -- <13.3>
  1, "table C has 1 values for 2 columns"
  -- </13.3>
})

-- 2015-04-12
--
test:do_execsql_test(14.1, [[
  WITH x AS (SELECT * FROM t) SELECT 0 EXCEPT SELECT 0 ORDER BY 1;
]], {
  -- <14.1>

  -- </14.1>
})

-- # 2015-05-27:  Do not allow rowid usage within a CTE
-- #
-- do_catchsql_test 15.1 {
--   WITH RECURSIVE
--     d(x) AS (VALUES(1) UNION ALL SELECT rowid+1 FROM d WHERE rowid<10)
--   SELECT x FROM d;
-- } {1 {no such column: rowid}}
-- 2015-07-05:  Do not allow aggregate recursive queries
--
test:do_catchsql_test(16.1, [[
  WITH RECURSIVE
    i(x) AS (VALUES(1) UNION SELECT count(*) FROM i)
  SELECT * FROM i;
]], {
  -- <16.1>
  1, "Tarantool does not support recursive aggregate queries"
  -- </16.1>
})

test:finish_test()

