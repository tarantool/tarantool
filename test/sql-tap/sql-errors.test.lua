#!/usr/bin/env tarantool
test = require("sqltester")
test:plan(45)

test:execsql([[
	CREATE TABLE t0 (i INT PRIMARY KEY, a INT);
	CREATE VIEW v0 AS SELECT * FROM t0;
]])
format = {}
for i = 1, 2001 do format[i] = {name = 'A' .. i, type = 'unsigned'} end
s0 = box.schema.space.create('S0', {format = format})
i0 = s0:create_index('I0')

test:do_catchsql_test(
	"sql-errors-1.1",
	[[
		ANALYZE v0;
	]], {
		-- <sql-errors-1.1>
		1,"Syntax error near 'ANALYZE'"
		-- </sql-errors-1.1>
	})

create_statement = 'CREATE TABLE t2 (i INT PRIMARY KEY'
for i = 1, 2001 do
	create_statement = create_statement .. ', s' .. i .. ' INT'
end
create_statement = create_statement .. ');'

test:do_catchsql_test(
	"sql-errors-1.2",
	create_statement,
	{
		-- <sql-errors-1.2>
		1,"Failed to create space 'T2': space column count 2001 exceeds the limit (2000)"
		-- </sql-errors-1.2>
	})

test:do_catchsql_test(
	"sql-errors-1.3",
	[[
		CREATE TABLE t3 (i INT PRIMARY KEY, a INT DEFAULT(MAX(i, 1)));
	]], {
		-- <sql-errors-1.3>
		1,"Failed to create space 'T3': default value of column 'A' is not constant"
		-- </sql-errors-1.3>
	})

test:do_catchsql_test(
	"sql-errors-1.4",
	[[
		CREATE TABLE t4 (i INT PRIMARY KEY, a INT PRIMARY KEY);
	]], {
		-- <sql-errors-1.4>
		1,"Failed to create space 'T4': primary key has been already declared"
		-- </sql-errors-1.4>
	})

test:do_catchsql_test(
	"sql-errors-1.5",
	[[
		CREATE TABLE t5 (i TEXT PRIMARY KEY AUTOINCREMENT);
	]], {
		-- <sql-errors-1.5>
		1,"Failed to create space 'T5': AUTOINCREMENT is only allowed on an INTEGER PRIMARY KEY or INT PRIMARY KEY"
		-- </sql-errors-1.5>
	})

test:do_catchsql_test(
	"sql-errors-1.6",
	[[
		CREATE TABLE t6 (i INT);
	]], {
		-- <sql-errors-1.6>
		1,"Failed to create space 'T6': PRIMARY KEY missing"
		-- </sql-errors-1.6>
	})

test:do_catchsql_test(
	"sql-errors-1.7",
	[[
		CREATE VIEW v7(a,b,c) AS SELECT * FROM t0;
	]], {
		-- <sql-errors-1.7>
		1,"Failed to create space 'V7': number of aliases doesn't match provided columns"
		-- </sql-errors-1.7>
	})

test:do_catchsql_test(
	"sql-errors-1.8",
	[[
		DROP VIEW t0;
	]], {
		-- <sql-errors-1.8>
		1,"Can't drop space 'T0': use DROP TABLE"
		-- </sql-errors-1.8>
	})

test:do_catchsql_test(
	"sql-errors-1.9",
	[[
		DROP TABLE v0;
	]], {
		-- <sql-errors-1.9>
		1,"Can't drop space 'V0': use DROP VIEW"
		-- </sql-errors-1.9>
	})

test:do_catchsql_test(
	"sql-errors-1.10",
	[[
		CREATE TABLE t10(i INT PRIMARY KEY REFERENCES v0);
	]], {
		-- <sql-errors-1.10>
		1,"Failed to create foreign key constraint 'FK_CONSTRAINT_1_T10': referenced space can't be VIEW"
		-- </sql-errors-1.10>
	})

test:do_catchsql_test(
	"sql-errors-1.11",
	[[
		CREATE VIEW v11 AS SELECT * FROM t0 WHERE i = ?;
	]], {
		-- <sql-errors-1.11>
		1,"Failed to create space 'V11': parameters are not allowed in views"
		-- </sql-errors-1.11>
	})

test:do_catchsql_test(
	"sql-errors-1.12",
	[[
		CREATE INDEX i12 ON v0(i);
	]], {
		-- <sql-errors-1.12>
		1,"Can't create or modify index 'I12' in space 'V0': views can not be indexed"
		-- </sql-errors-1.12>
	})

test:do_catchsql_test(
	"sql-errors-1.13",
	[[
		SELECT 9223372036854775808;
	]], {
		-- <sql-errors-1.13>
		1,"Integer literal 9223372036854775808 exceeds the supported range -9223372036854775808 - 9223372036854775807"
		-- </sql-errors-1.13>
	})

test:do_catchsql_test(
	"sql-errors-1.14",
	[[
		SELECT 0x10000000000000000;
	]], {
		-- <sql-errors-1.14>
		1,"Hex literal 0x10000000000000000 length 17 exceeds the supported limit (16)"
		-- </sql-errors-1.14>
	})

select_statement = 'SELECT i FROM t0 WHERE i = 0'
for i = 1, 200 do
	select_statement = select_statement .. ' OR i = ' .. i
end
select_statement = select_statement .. ';'

test:do_catchsql_test(
	"sql-errors-1.15",
	select_statement,
	{
		-- <sql-errors-1.15>
		1,"Number of nodes in expression tree 201 exceeds the limit (200)"
		-- </sql-errors-1.15>
	})

select_statement = 'SELECT CHAR(1'
for i = 1, 127 do
	select_statement = select_statement .. ', ' .. i
end
select_statement = select_statement .. ');'

test:do_catchsql_test(
	"sql-errors-1.16",
	select_statement,
	{
		-- <sql-errors-1.16>
		1,"Number of arguments to function CHAR 128 exceeds the limit (127)"
		-- </sql-errors-1.16>
	})

select_statement = 'SELECT MAX(1'
for i = 1, 127 do
	select_statement = select_statement .. ', ' .. i
end
select_statement = select_statement .. ');'

test:do_catchsql_test(
	"sql-errors-1.17",
	select_statement,
	{
		-- <sql-errors-1.17>
		1,"Number of arguments to function MAX 128 exceeds the limit (127)"
		-- </sql-errors-1.17>
	})

select_statement = 'SELECT 0'
for i = 1, 30 do
	select_statement = select_statement .. ' UNION ALL SELECT ' .. i
end
select_statement = select_statement .. ';'

test:do_catchsql_test(
	"sql-errors-1.18",
	select_statement,
	{
		-- <sql-errors-1.18>
		1,"The number of UNION or EXCEPT or INTERSECT operations 31 exceeds the limit (30)"
		-- </sql-errors-1.18>
	})

select_statement = 'SELECT 0'
for i = 1, 2000 do
	select_statement = select_statement .. ', ' .. i
end
select_statement = select_statement .. ';'

test:do_catchsql_test(
	"sql-errors-1.19",
	select_statement,
	{
		-- <sql-errors-1.19>
		1,"The number of columns in result set 2001 exceeds the limit (2000)"
		-- </sql-errors-1.19>
	})

select_statement = 'SELECT * FROM t0'
for i = 1, 64 do
	select_statement = select_statement .. ', t0 as t' .. i
end
select_statement = select_statement .. ';'

test:do_catchsql_test(
	"sql-errors-1.20",
	select_statement,
	{
		-- <sql-errors-1.20>
		1,"The number of tables in a join 65 exceeds the limit (64)"
		-- </sql-errors-1.20>
	})

test:do_catchsql_test(
	"sql-errors-1.21",
	[[
		SELECT $65001;
	]], {
		-- <sql-errors-1.21>
		1,"SQL bind parameter limit reached: 65000"
		-- </sql-errors-1.21>
	})

select_statement = 'SELECT '..string.rep('?, ', box.schema.SQL_BIND_PARAMETER_MAX)..'?;'

test:do_catchsql_test(
	"sql-errors-1.22",
	select_statement,
	{
		-- <sql-errors-1.22>
		1,"SQL bind parameter limit reached: 65000"
		-- </sql-errors-1.22>
	})

test:do_catchsql_test(
	"sql-errors-1.23",
	[[
		INSERT INTO v0 VALUES (2);
	]], {
		-- <sql-errors-1.23>
		1,"Can't modify space 'V0': space is a view"
		-- </sql-errors-1.23>
	})

test:do_catchsql_test(
	"sql-errors-1.24",
	[[
		UPDATE v0 SET i = 2 WHERE i = 1;
	]], {
		-- <sql-errors-1.24>
		1,"Can't modify space 'V0': space is a view"
		-- </sql-errors-1.24>
	})

test:do_catchsql_test(
	"sql-errors-1.25",
	[[
		DELETE FROM v0;
	]], {
		-- <sql-errors-1.25>
		1,"Can't modify space 'V0': space is a view"
		-- </sql-errors-1.25>
	})

test:do_catchsql_test(
	"sql-errors-1.26",
	[[
		CREATE TABLE t26 (i INT, PRIMARY KEY('i'));
	]], {
		-- <sql-errors-1.26>
		1,"Expressions are prohibited in an index definition"
		-- </sql-errors-1.26>
	})

test:do_catchsql_test(
	"sql-errors-1.27",
	[[
		CREATE TABLE t27 (i INT PRIMARY KEY, CHECK(i < (SELECT * FROM t0)));
	]], {
		-- <sql-errors-1.27>
		1,"Failed to create space 'T27': Subqueries are prohibited in a CHECK constraint definition"
		-- </sql-errors-1.27>
	})

test:do_catchsql_test(
	"sql-errors-1.28",
	[[
		CREATE INDEX i28 ON t0(t0.i);
	]], {
		-- <sql-errors-1.28>
		1,"Expressions are prohibited in an index definition"
		-- </sql-errors-1.28>
	})

test:do_catchsql_test(
	"sql-errors-1.29",
	[[
		CREATE INDEX i29 ON t0($1);
	]], {
		-- <sql-errors-1.29>
		1,"Parameter markers are prohibited in an index definition"
		-- </sql-errors-1.29>
	})

create_index_statement = 'CREATE INDEX i30 on t0(i'..string.rep(', i', 2000)..');'

test:do_catchsql_test(
	"sql-errors-1.30",
	create_index_statement,
	{
		-- <sql-errors-1.30>
		1,"The number of columns in index 2001 exceeds the limit (2000)"
		-- </sql-errors-1.30>
	})

update_statement = 'UPDATE s0 SET a1 = a1 + 1'
for i = 2, 2001 do
	update_statement = update_statement .. ', a' .. i .. ' = a' .. i .. ' + 1'
end
update_statement = update_statement .. ';'

test:do_catchsql_test(
	"sql-errors-1.31",
	update_statement,
	{
		-- <sql-errors-1.31>
		1,"The number of columns in set list 2001 exceeds the limit (2000)"
		-- </sql-errors-1.31>
	})

select_statement = 'SELECT * FROM (SELECT 1 UNION ALL SELECT 1 ORDER BY 1'..string.rep(', 1', 2000)..')'

test:do_catchsql_test(
	"sql-errors-1.32",
	select_statement,
	{
		-- <sql-errors-1.32>
		1,"The number of terms in ORDER BY clause 2001 exceeds the limit (2000)"
		-- </sql-errors-1.32>
	})

select_statement = 'SELECT 1 ORDER BY 1'..string.rep(', 1', 2000)

test:do_catchsql_test(
	"sql-errors-1.33",
	select_statement,
	{
		-- <sql-errors-1.33>
		1,"The number of terms in ORDER BY clause 2001 exceeds the limit (2000)"
		-- </sql-errors-1.33>
	})

select_statement = 'SELECT 1 as '..string.rep('x', 65001)

test:do_catchsql_test(
	"sql-errors-1.34",
	select_statement,
	{
		-- <sql-errors-1.34>
		1,"Invalid identifier 'XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX' (expected printable symbols only or it is too long)"
		-- </sql-errors-1.34>
	})

test:do_catchsql_test(
	"sql-errors-1.35",
	[[
		SELECT 1 as "";
	]], {
		-- <sql-errors-1.35>
		1,"Invalid identifier '' (expected printable symbols only or it is too long)"
		-- </sql-errors-1.35>
	})

test:do_catchsql_test(
	"sql-errors-1.36",
	[[
		SELECT likelihood(1, 2);
	]], {
		-- <sql-errors-1.36>
		1,"Illegal parameters, second argument to likelihood() must be a constant between 0.0 and 1.0"
		-- </sql-errors-1.36>
	})

test:do_catchsql_test(
	"sql-errors-1.37",
	[[
		CREATE TRIGGER r0 AFTER INSERT ON t0 FOR EACH ROW BEGIN INSERT INTO t0.i VALUES (2); END;
	]], {
		-- <sql-errors-1.37>
		1,"qualified table names are not allowed on INSERT, UPDATE, and DELETE statements within triggers"
		-- </sql-errors-1.37>
	})

test:do_catchsql_test(
	"sql-errors-1.38",
	[[
		UPDATE t0 SET (i, a) = (100,1,1);
	]], {
		-- <sql-errors-1.38>
		1,"2 columns assigned 3 values"
		-- </sql-errors-1.38>
	})

test:do_catchsql_test(
	"sql-errors-1.39",
	[[
		SELECT * FROM t0();
	]], {
		-- <sql-errors-1.39>
		1,"'T0' is not a function"
		-- </sql-errors-1.39>
	})

test:do_catchsql_test(
	"sql-errors-1.40",
	[[
		SELECT $0;
	]], {
		-- <sql-errors-1.40>
		1,"Index of binding slots must start from 1"
		-- </sql-errors-1.40>
	})

test:do_catchsql_test(
	"sql-errors-1.41",
	[[
		SELECT (1,2,3) == (1,2,3,4);
	]], {
		-- <sql-errors-1.41>
		1,"row value misused"
		-- </sql-errors-1.41>
	})

test:do_catchsql_test(
	"sql-errors-1.42",
	[[
		SELECT (1, 2);
	]], {
		-- <sql-errors-1.42>
		1,"row value misused"
		-- </sql-errors-1.42>
	})

test:do_catchsql_test(
	"sql-errors-1.43",
	[[
		SELECT (i,a) AS m FROM t0 WHERE m < 1;
	]], {
		-- <sql-errors-1.43>
		1,"row value misused"
		-- </sql-errors-1.43>
	})

test:do_execsql_test(
	"sql-errors-1.44",
	[[
		SELECT (1, 2, 3) < (1, 2, 4);
	]], {
		-- <sql-errors-1.44>
		1
		-- </sql-errors-1.44>
	})

test:do_execsql_test(
	"sql-errors-1.45",
	[[
		SELECT (1, 2, 3) < (1, 2, 2);
	]], {
		-- <sql-errors-1.45>
		0
		-- </sql-errors-1.45>
	})

test:finish_test()
