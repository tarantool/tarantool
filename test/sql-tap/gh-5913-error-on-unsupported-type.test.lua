#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(6)

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

test_space:drop()

test:finish_test()
