#!/usr/bin/env tarantool
local test = require("sqltester")

test:plan(4)

local tt = {}
local table_word = "АААААААААА"

-- Create 30kb table name
for i=1,300 do
	table.insert(tt, table_word)
end

local table_name = table.concat(tt)

-- Execute CREATE TABLE statement with 30kb table identifier
test:do_execsql_test(
	"identifier-1.1",
	"CREATE TABLE " .. table_name .. "(a INT PRIMARY KEY);"
	, {
	    -- <identifier-1.1>

	    -- <identifier-1.1>
	})

local vt = {}
local view_word = "BBBBBBBBBB"

-- Create 30kb view name
for i=1, 300 do
	table.insert(vt, view_word)
end

local view_name = table.concat(vt)

test:do_execsql_test(
	"identifier-1.2",
	"CREATE VIEW " .. view_name .. " AS SELECT 1; "
	, {
	    -- <identifier-1.2>
	    -- <identifier-1.2>
	})

local it = {}
local index_word = "ЕЕЕЕЕЕЕЕЕЕ"

-- Create 30kb index name
for i=1, 300 do
	table.insert(it, index_word)
end

local index_name = table.concat(it)

local field_table = {}
local field_word = 'ДДДДДДДДД'

-- Create 30kb field name
for i=1, 300 do
	table.insert(field_table, field_word)
end

local field_name = table.concat(field_table)
local create_table = "CREATE TABLE t1(" .. field_name .. " INT PRIMARY KEY);"

test:execsql(create_table)
test:do_execsql_test(
	"identifier-1.3",
	"CREATE INDEX " .. index_name .. " ON t1(" .. field_name .. ");"
	, {
	   -- <identifier-1.3>
	   --
	})


local trig_table = {}
local trigger_word = "ССССССССС"

for i=1, 300 do
	table.insert(trig_table, trigger_word)
end

local trigger_name = table.concat(trig_table)

test:do_execsql_test(
	"identifier-1.4",
	"CREATE TRIGGER " .. trigger_name ..
	[[
	BEFORE UPDATE ON t1
	FOR EACH ROW
    BEGIN
		SELECT 1;
	END;
	]]
	, {
	    -- <identifier-1.4>
	    -- <identifier-1.5>
	})

test:finish_test()
