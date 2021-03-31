#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(43)

--
-- gh-5913: Report an error on unsupported type in request result
--

local test_space = box.schema.create_space('T', {
	format = {{name = 'id', type = 'number'},
		  {name = 'uid', type = 'uuid'},
		  {name = 'dec', type = 'decimal'},
		  {name = 'arr', type = 'array'}}
})

test_space:create_index('pri')
test_space:insert{1,
		  require('uuid').fromstr('03511f76-d562-459b-a670-c8489b2545fd'),
		  require('decimal').new('100.1'),
		  {1, 2}}
-- Lua interface should successfully select
test_space:select()
-- Should not fail on supported fields
test:do_execsql_test(
    "gh-5913.1",
    [[
	SELECT "id" FROM T;
    ]], { 1 } )
test:do_execsql_test(
    "gh-5913.2",
    [[
	SELECT "arr" FROM T;
    ]], { 1, 2 } )
-- Should fail on unsupported fields
test:do_catchsql_test(
    "gh-5913.3",
    [[
	SELECT "uid" FROM T;
    ]], { 1, "SQL does not support uuid" } )
test:do_catchsql_test(
    "gh-5913.4",
    [[
	select "dec" from T
    ]], { 1, "SQL does not support decimal"} )
-- Should fail on unsupported in complex select
test:do_catchsql_test(
    "gh-5913.5",
    [[
	SELECT * FROM T;
    ]], { 1, "SQL does not support uuid" } )

-- Should fail on unsupported in update
test:do_catchsql_test(
    "gh-5913.6",
    [[
	UPDATE T SET "dec" = 3;
    ]], { 1, "SQL does not support uuid" } )

-- Should fail on all built-ins
test:do_catchsql_test(
    "gh-5913.7",
    [[
	SELECT TRIM("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_execsql_test(
    "gh-5913.8",
    [[
	SELECT TYPEOF("dec") FROM T;
    ]], { "decimal" } )

test:do_execsql_test(
    "gh-5913.9",
    [[
	SELECT PRINTF("dec", "dec") FROM T;
    ]], { "100.1" } )

test:do_catchsql_test(
    "gh-5913.10",
    [[
	SELECT UNICODE("dec") FROM T;
    ]], { 0, {49} } )

test:do_catchsql_test(
    "gh-5913.11",
    [[
	SELECT CHAR("dec", "dec") FROM T;
    ]], { 0, {"\000\000"} } )

test:do_catchsql_test(
    "gh-5913.12",
    [[
	SELECT HEX("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.14",
    [[
	SELECT QUOTE("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

-- intentionally put an incorrect select below, test crashes if remove '-' in first
-- argument
test:do_catchsql_test(
    "gh-5913.15",
    [[
	SELECT REPLACE("dec-", "dec", "dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.16",
    [[
	SELECT SUBSTR("dec", "dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.17",
    [[
	SELECT GROUP_CONCAT("dec", "dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.26",
    [[
	SELECT LENGTH("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.27",
    [[
	SELECT POSITION("dec", "dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.28",
    [[
	SELECT ROUND("dec", "dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.29",
    [[
	SELECT UPPER("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.30",
    [[
	SELECT LOWER("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.31",
    [[
	SELECT IFNULL("dec", "dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.35",
    [[
	SELECT CHARACTER_LENGTH("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.36",
    [[
	SELECT CHAR_LENGTH("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.41",
    [[
	SELECT COUNT("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.42",
    [[
	SELECT "dec" LIKE "dec" FROM T;
    ]], { 1, "Inconsistent types: expected text got varbinary" } )

test:do_catchsql_test(
    "gh-5913.43",
    [[
	SELECT ABS("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.48",
    [[
	SELECT SUM("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.49",
    [[
	SELECT TOTAL("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.50",
    [[
	SELECT AVG("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.51",
    [[
	SELECT RANDOMBLOB("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.52",
    [[
	SELECT NULLIF("dec", "dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.53",
    [[
	SELECT ZEROBLOB("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.54",
    [[
	SELECT MIN("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.55",
    [[
	SELECT MAX("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.56",
    [[
	SELECT COALESCE("dec", "dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.63",
    [[
	SELECT SOUNDEX("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.64",
    [[
	SELECT LIKELIHOOD("dec", 0.5) FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.65",
    [[
	SELECT LIKELY("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.66",
    [[
	SELECT UNLIKELY("dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

-- intentionally put an incorrect select below, test crashes if remove '-' in first
-- argument
test:do_catchsql_test(
    "gh-5913.67",
    [[
	SELECT LUA("dec-") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.68",
    [[
	SELECT GREATEST("dec", "dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )

test:do_catchsql_test(
    "gh-5913.69",
    [[
	SELECT LEAST("dec", "dec") FROM T;
    ]], { 1, "SQL does not support decimal" } )


test_space:drop()

test:finish_test()
