#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(19)

local test_prefix = "gh-2931-savepoints-"

test:do_catchsql_test(
	test_prefix.."1.0.1",
	"CREATE TABLE t1 (a int primary key);")

test:do_catchsql_test(
	test_prefix.."1.0.2",
	"CREATE TABLE t2 (a int primary key);")

test:do_catchsql_test(
	test_prefix.."1.0.3",
	[[
		CREATE TRIGGER TRIG1 BEFORE INSERT ON T1
		begin
			insert into t2 values(new.a);
		end;]],
	{0})

local testcases = {
	{"1",
		[[insert into t1 values(1);
		select * from t1 union all select * from t2;]],
		{0,{1,1}} },
	{"3",
		[[START TRANSACTION;
		savepoint s1_1;
		savepoint s1_2;]],
		{0} },
	{"4",
		[[rollback to savepoint s1_1;
		rollback to savepoint s1_1;
		select * from t1 union all select * from t2;]],
		{0,{1,1}}},
	{"5",
		[[rollback to savepoint s1_2;]],
		{1, "no such savepoint: S1_2"}},
	{"6",
		[[insert into t1 values(2);
		select * from t1 union all select * from t2;]],
		{0,{1,2,1,2}}},
	{"7",
		[[rollback to savepoint s1_1;
		select * from t1 union all select * from t2;]],
		{0,{1,1}}},
	{"8",
		[[insert into t1 values(5),(6),(98);
		rollback to savepoint s1_1;
		select * from t1 union all select * from t2;]],
		{0,{1,1}}},
	{"9",
		[[insert into t1 values(2);
		insert into t2 values(4);
		savepoint s2;
		select * from t1 union all select * from t2;]],
		{0,{1,2,1,2,4}}},
	{"10",
		[[insert into t1 values(10); savepoint s10;
		insert into t1 values(11); savepoint s11;
		insert into t1 values(12); savepoint s12;
		insert into t1 values(13); savepoint s13;
		insert into t1 values(14); savepoint s14;
		select * from t1 union all select * from t2;]],
		{0,{1,2,10,11,12,13,14,1,2,4,10,11,12,13,14}}},
	{"11",
		[[rollback to savepoint s13;
		select * from t1 union all select * from t2;]],
		{0,{1,2,10,11,12,13,1,2,4,10,11,12,13}}},
	{"12",
		[[rollback to savepoint s12;
		select * from t1 union all select * from t2;]],
		{0,{1,2,10,11,12,1,2,4,10,11,12}}},
	{"13",
		[[rollback to savepoint s11;
		select * from t1 union all select * from t2;]],
		{0,{1,2,10,11,1,2,4,10,11}}},
	{"14",
		[[insert into t1 values(4);]],
		{1,"Duplicate key exists in unique index 'pk_unnamed_T2_1' in space 'T2'"}},
	{"15",
		[[select * from t1 union all select * from t2;]],
		{0,{1,2,10,11,1,2,4,10,11}}},
	{"16",
		[[insert or rollback into t1 values(4);]],
		{1,"Duplicate key exists in unique index 'pk_unnamed_T2_1' in space 'T2'"}},
	{"17",  -- should work as transaction is rolled back
		[[insert or rollback into t1 values(4);
		select * from t1 union all select * from t2;]],
		{0,{1,4,1,4}}},
}


test:do_catchsql_set_test(
	testcases,
	test_prefix.."1.")



test:finish_test()
