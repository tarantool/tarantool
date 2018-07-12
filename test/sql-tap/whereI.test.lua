#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(7)

--!./tcltestrunner.lua
-- 2014-03-31
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
-------------------------------------------------------------------------
-- The focus of this file is testing the OR optimization on WITHOUT ROWID 
-- tables.
--
-- Note: Tarantool has no rowid support, so all tables are WITHOUT ROWID.
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
testprefix = "whereI"
test:do_execsql_test(1.0, [[
    CREATE TABLE t1(a INT, b TEXT, c TEXT, PRIMARY KEY(a));
    INSERT INTO t1 VALUES(1, 'a', 'z');
    INSERT INTO t1 VALUES(2, 'b', 'y');
    INSERT INTO t1 VALUES(3, 'c', 'x');
    INSERT INTO t1 VALUES(4, 'd', 'w');
    CREATE INDEX i1 ON t1(b);
    CREATE INDEX i2 ON t1(c);
]])

-- do_eqp_test 1.1 {
--   SELECT a FROM t1 WHERE b='b' OR c='x'
-- } {
--   0 0 0 {SEARCH TABLE t1 USING INDEX i1 (b=?)} 
--   0 0 0 {SEARCH TABLE t1 USING INDEX i2 (c=?)}
-- }
test:do_execsql_test(1.2, [[
    SELECT a FROM t1 WHERE b='b' OR c='x'
]], {
    -- <1.2>
    2, 3
    -- </1.2>
})

test:do_execsql_test(1.3, [[
    SELECT a FROM t1 WHERE b='a' OR c='z'
]], {
    -- <1.3>
    1
    -- </1.3>
})

------------------------------------------------------------------------
-- Try that again, this time with non integer PRIMARY KEY values.
--
test:do_execsql_test(2.0, [[
    CREATE TABLE t2(a TEXT, b TEXT, c TEXT, PRIMARY KEY(a));
    INSERT INTO t2 VALUES('i', 'a', 'z');
    INSERT INTO t2 VALUES('ii', 'b', 'y');
    INSERT INTO t2 VALUES('iii', 'c', 'x');
    INSERT INTO t2 VALUES('iv', 'd', 'w');
    CREATE INDEX i3 ON t2(b);
    CREATE INDEX i4 ON t2(c);
]])

-- do_eqp_test 2.1 {
--   SELECT a FROM t2 WHERE b='b' OR c='x'
-- } {
--   0 0 0 {SEARCH TABLE t2 USING INDEX i3 (b=?)} 
--   0 0 0 {SEARCH TABLE t2 USING INDEX i4 (c=?)}
-- }
test:do_execsql_test(2.2, [[
    SELECT a FROM t2 WHERE b='b' OR c='x'
]], {
    -- <2.2>
    "ii", "iii"
    -- </2.2>
})

test:do_execsql_test(2.3, [[
    SELECT a FROM t2 WHERE b='a' OR c='z'
]], {
    -- <2.3>
    "i"
    -- </2.3>
})

------------------------------------------------------------------------
-- On a table with a multi-column PK.
--
test:do_execsql_test(3.0, [[
    CREATE TABLE t3(a TEXT, b INT, c INT, d TEXT, PRIMARY KEY(c, b));

    INSERT INTO t3 VALUES('f', 1, 1, 'o');
    INSERT INTO t3 VALUES('o', 2, 1, 't');
    INSERT INTO t3 VALUES('t', 1, 2, 't');
    INSERT INTO t3 VALUES('t', 2, 2, 'f');

    CREATE INDEX t3i1 ON t3(d);
    CREATE INDEX t3i2 ON t3(a);

    SELECT CAST(c AS TEXT)||'.'||CAST(b AS TEXT) FROM t3 WHERE a='t' OR d='t'
]], {
    -- <3.0>
    '2.1', '2.2', '1.2'
    -- </3.0>
})

test:finish_test()

