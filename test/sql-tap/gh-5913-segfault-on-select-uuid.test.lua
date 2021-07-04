#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(6)

local uuid = require("uuid").fromstr("11111111-1111-1111-1111-111111111111")
local decimal = require("decimal").new(111.111)

box.schema.create_space('T')
box.space.T:format({{name = "I", type = "integer"}, {name = "U", type = "uuid"},
                    {name = "D", type = "decimal"}})
box.space.T:create_index("primary")
box.space.T:insert({1, uuid, decimal})

--
-- Make sure that there is no segmentation fault on select from field that
-- contains UUID or DECIMAL. Currently SQL does not support UUID and DECIMAL,
-- so they treated as VARBINARY.
--
test:do_execsql_test(
    "gh-5913-1",
    [[
        SELECT i, u, d FROM t;
        SELECT i from t;
    ]], {
        1
    })

box.schema.create_space('T1')
box.space.T1:format({{name = "I", type = "integer"},
                     {name = "U", type = "uuid", is_nullable = true},
                     {name = "D", type = "decimal", is_nullable = true}})
box.space.T1:create_index("primary")

--
-- Since SQL does not support UUID and DECIMAL and they treated as VARBINARY,
-- they cannot be inserted from SQL.
--
test:do_catchsql_test(
    "gh-5913-2",
    [[
        INSERT INTO t1 SELECT i, u, NULL FROM t;
    ]], {
        1, "Type mismatch: can not convert varbinary(x'D80211111111111111111111111111111111') to uuid"
    })

test:do_catchsql_test(
    "gh-5913-3",
    [[
        INSERT INTO t1 SELECT i, NULL, d FROM t;
    ]], {
        1, "Type mismatch: can not convert varbinary(x'C70501030111111C') to decimal"
    })

--
-- Still, if UUID or DECIMAL fields does not selected directly, insert is
-- working properly.
--
test:do_execsql_test(
    "gh-5913-4",
    [[
        INSERT INTO t1 SELECT * FROM t;
        SELECT count() FROM t1;
    ]], {
        1
    })

box.schema.create_space('TU')
box.space.TU:format({{name = "I", type = "integer"},
                     {name = "U", type = "uuid"}})
box.space.TU:create_index("primary")
box.space.TU:insert({1, uuid})

box.schema.create_space('TD')
box.space.TD:format({{name = "I", type = "integer"},
                     {name = "D", type = "decimal"}})
box.space.TD:create_index("primary")
box.space.TD:insert({1, decimal})

--
-- Update of UUID or VARBINARY also does not lead to segfault, however throws an
-- error since after changing value cannot be inserted into the field from SQL.
--
test:do_catchsql_test(
    "gh-5913-5",
    [[
        UPDATE tu SET u = u;
    ]], {
        1, "Type mismatch: can not convert varbinary(x'D80211111111111111111111111111111111') to uuid"
    })

test:do_catchsql_test(
    "gh-5913-6",
    [[
        UPDATE td SET d = d;
    ]], {
        1, "Type mismatch: can not convert varbinary(x'C70501030111111C') to decimal"
    })

test:finish_test()
