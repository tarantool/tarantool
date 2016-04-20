#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('netbox')

local net_box = require('net.box')
local test_run = require('test_run')
local inspector = test_run.new()

test:plan(4)

-- create tarantool instance
test:is(
    inspector:cmd("create server second with script='box/box.lua'\n"),
    true, 'instance created'
)
test:is(
    inspector:cmd('start server second'),
    true, 'instance started'
)

-- check that net.box is correct without box.cfg{}
local uri = inspector:eval('second', 'box.cfg.listen')[1]
local conn = net_box:new(uri)
test:is(conn:is_connected(), true, 'connected to instance')
test:is(conn.space ~= nil, true, 'space exists')

-- cleanup
conn:close()
inspector:cmd('stop server second with cleanup=1')
test:check()
os.exit(0)
