#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('gh-5210-table-clear')

test:plan(2)
local t = {a = 1, b = 2}
test:is(table.clear(t), nil, 'table clear')
test:is_deeply(t, {}, 'table is clear')

os.exit(test:check() and 0 or 1)
