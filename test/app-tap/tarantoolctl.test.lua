#!/usr/bin/env tarantool

local test = require('tap').test('cfg')
test:plan(1)

-------------------------------------------------------------------------------
-- gh-1293: permission denied on tarantoolctl enter
-------------------------------------------------------------------------------

-- test-run uses tarantoolctl under the hood
local console_sock = 'box.control'
local mode = require('fio').stat(console_sock).mode
test:is(string.format("%o", mode), "140664",
    "gh1293: permission denied on tarantoolctl enter")

test:check()
os.exit(0)
