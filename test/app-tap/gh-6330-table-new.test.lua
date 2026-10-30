#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('gh-6330-table-new')

test:plan(2)
local t = table.new(16, 0)
test:ok(t, 'table new')

-- The following test relies on the internal table representaion and on
-- the way LuaJIT calculates the table lenght. Without preallocation
-- it would be #t == 0.
t[16] = true
test:is(#t, 16, 'preallocation works')

os.exit(test:check() and 0 or 1)
