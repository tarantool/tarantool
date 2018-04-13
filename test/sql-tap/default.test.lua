#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(11)

--!./tcltestrunner.lua
-- 2005 August 18
--
-- The author disclaims copyright to this source code.  In place of
-- a legal notice, here is a blessing:
--
--    May you do good and not evil.
--    May you find forgiveness for yourself and forgive others.
--    May you share freely, never taking more than you give.
--
---------------------------------------------------------------------------
-- This file implements regression tests for SQLite library.  The
-- focus of this file is testing corner cases of the DEFAULT syntax
-- on table definitions.
--
-- $Id: default.test,v 1.3 2009/02/19 14:39:25 danielk1977 Exp $
--
-- ["set","testdir",[["file","dirname",["argv0"]]]]
-- ["source",[["testdir"],"\/tester.tcl"]]
-- MUST_WORK_TEST
test:do_execsql_test(
	"default-1.1",
	[[
		CREATE TABLE t1(
		rowid INTEGER PRIMARY KEY AUTOINCREMENT, 
		a INTEGER,
		b BLOB DEFAULT x'6869'
		);
		INSERT INTO t1(a) VALUES(1);
		SELECT a, b from t1;
	]], {
	-- <default-1.1>
	1, "hi"
	-- </default-1.1>
})



test:do_execsql_test(
	"default-1.2",
	[[
	CREATE TABLE t2(
	rowid INTEGER PRIMARY KEY AUTOINCREMENT, 
	x INTEGER,
	y INTEGER DEFAULT NULL
	);
	INSERT INTO t2(x) VALUES(1);
	SELECT x,y FROM t2;
	]], {
	-- <default-1.2>
	1, ""
	-- </default-1.2>
})

test:do_catchsql_test(
	"default-1.3",
	[[
	CREATE TABLE t3(
	rowid INTEGER PRIMARY KEY AUTOINCREMENT, 
	x INTEGER,
	y INTEGER DEFAULT (max(x,5))
	);
	]], {
	-- <default-1.3>
	1, "default value of column [Y] is not constant"
	-- </default-1.3>
})

test:do_execsql_test(
	"default-2.1",
	[[
	CREATE TABLE t4(
	rowid INTEGER PRIMARY KEY AUTOINCREMENT, 
	c DEFAULT 'abc'
	);
	PRAGMA table_info(t4);
	]], {
	-- <default-2.1>
	0,"ROWID","integer",1,"",1,1,"C","scalar",0,"'abc'",0
	-- </default-2.1>
})

test:do_execsql_test(
	"default-2.2",
	[[
	INSERT INTO t4 DEFAULT VALUES;
	PRAGMA table_info(t4);
	]], {
	-- <default-2.2>
	0,"ROWID","integer",1,"",1,1,"C","scalar",0,"'abc'",0
	-- </default-2.2>
})



test:do_execsql_test(
	"default-3.1",
	[[
	CREATE TABLE t3(
	a INTEGER PRIMARY KEY AUTOINCREMENT,
	b INT DEFAULT 12345 UNIQUE NOT NULL CHECK( b>=0 AND b<99999 ),
	c VARCHAR(123,456) DEFAULT 'hello' NOT NULL,
	d REAL,
	e FLOATING POINT(5,10) DEFAULT 4.36,
	f NATIONAL CHARACTER(15), --COLLATE RTRIM,
	g LONG INTEGER DEFAULT( 3600*12 )
	);
	INSERT INTO t3 VALUES(null, 5, 'row1', '5.25', 'xyz', 321, '432');
	SELECT a, typeof(a), b, typeof(b), c, typeof(c), 
	d, typeof(d), e, typeof(e), f, typeof(f),
	g, typeof(g) FROM t3;
	]], {
	-- <default-3.1>
	-- TODO: In original test "321" is not a string, its a value.
	-- In current situation I don't know what to do, need Kirill's
	-- advice.
	-- Bulat
	1, "integer", 5, "integer", "row1", "text", 5.25, "real", "xyz", "text", "321", "text", 432, "integer"
	-- </default-3.1>
})

test:do_execsql_test(
	"default-3.2",
	[[
	DELETE FROM t3;
	INSERT INTO t3 DEFAULT VALUES;
	SELECT * FROM t3;
	]], {
	-- <default-3.2>
	2, 12345, "hello", "", 4.36, "", 43200
	-- </default-3.2>
})

test:do_execsql_test(
	"default-3.3",
	[[
	CREATE TABLE t300(
	pk INTEGER PRIMARY KEY AUTOINCREMENT,
	a INT DEFAULT 2147483647,
	b INT DEFAULT 2147483648,
	c INT DEFAULT +9223372036854775807,
	d INT DEFAULT -2147483647,
	e INT DEFAULT -2147483648,
	f INT DEFAULT (-9223372036854775808),
	g INT DEFAULT 9223372036854775808,
	h INT DEFAULT (-(-9223372036854775807))
	);
	INSERT INTO t300 DEFAULT VALUES;
	SELECT a, b, c, d, e, f, cast(g as text), h FROM t300;
	]], {
	-- <default-3.3>
	2147483647, 2147483648, 9223372036854775807LL, -2147483647, -2147483648, -9223372036854775808LL, "9.22337203685478e+18", 9223372036854775807LL
	-- </default-3.3>
})

-- Do now allow bound parameters in new DEFAULT values. 
-- Silently convert bound parameters to NULL in DEFAULT causes
-- in the sqlite_master table, for backwards compatibility.
--
test:execsql("DROP TABLE IF EXISTS t1")
test:execsql("DROP TABLE IF EXISTS t2")

test:do_catchsql_test(
	"default-4.1",
	[[
	CREATE TABLE t2(
	rowid INTEGER PRIMARY KEY AUTOINCREMENT,
	a TEXT,
	b TEXT DEFAULT(:xyz)
	);
	]], {
	-- <default-4.2>
	1, "default value of column [B] is not constant"
	-- </default-4.2>
})

test:do_catchsql_test(
	"default-4.2",
	[[
	CREATE TABLE t2(
	rowid INTEGER PRIMARY KEY AUTOINCREMENT,
	a TEXT,
	b TEXT DEFAULT(abs(:xyz))
	);
	]], {
	-- <default-4.3>
	1, "default value of column [B] is not constant"
	-- </default-4.3>
})

test:do_catchsql_test(
	"default-4.3",
	[[
	CREATE TABLE t2(
	rowid INTEGER PRIMARY KEY AUTOINCREMENT,
	a TEXT,
	b TEXT DEFAULT(98+coalesce(5,:xyz))
	);
	]], {
	-- <default-4.4>
	1, "default value of column [B] is not constant"
	-- </default-4.4>
})



test:finish_test()
