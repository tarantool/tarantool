#!/usr/bin/env tarantool
local test = require("sqltester")
test:plan(4)

-- gh-4782 - Raise an error in case space features HASH index.
-- Make sure that in case of attempt to use HASH index
-- from within SQL statement - an error is raised.
-- This is actually a stub until we are unable to employ
-- HASH index while planning a query.

local f = {
    {'1', 'unsigned'},
    {'2', 'string'},
    {'3', 'array'}
}

local s = box.schema.create_space("T1", {format = f})
s:create_index('PK', {type = 'hash', parts = {'1'}})

test:do_catchsql_test(
    "blocked-hash-index-1",
    "SELECT * FROM T1", {
        1, "SQL does not support using non-TREE index type. Please, use INDEXED BY clause to force using proper index."
    })

s = box.schema.create_space("T2", {format = f})
s:create_index('PK', {parts = {'2'}})
s:create_index('SK1', {type = 'hash', parts = {'1'}})
s:create_index('SK2', {type = 'bitset', parts = {'1'}})
s:create_index('SK3', {type = 'rtree', parts = {'3'}})

test:do_catchsql_test(
    "blocked-hash-index-2",
    "SELECT * FROM T2 INDEXED BY SK1", {
        1, "SQL does not support using non-TREE index type. Please, use INDEXED BY clause to force using proper index."
    })

test:do_catchsql_test(
    "blocked-hash-index-3",
    "SELECT * FROM T2 INDEXED BY SK2", {
        1, "SQL does not support using non-TREE index type. Please, use INDEXED BY clause to force using proper index."
    })

test:do_catchsql_test(
    "blocked-hash-index-4",
    "SELECT * FROM T2 INDEXED BY SK3", {
        1, "SQL does not support using non-TREE index type. Please, use INDEXED BY clause to force using proper index."
    })

test:finish_test()
