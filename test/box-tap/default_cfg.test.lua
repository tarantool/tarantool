#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('cfg')
test:plan(2)
test:ok(type(box.space) == 'table', "box without cfg not failed")
test:ok(type(box.space._space) == 'table', "default cfg is run")
test:check()
os.exit(0)