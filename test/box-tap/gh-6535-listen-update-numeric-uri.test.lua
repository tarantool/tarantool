#!/usr/bin/env tarantool

local tap = require('tap')
local net_box = require('net.box')
local os = require('os')

local test = tap.test('gh-6535-listen-update-numeric-uri')
test:plan(2)
box.cfg{listen = "unix/:./tarantoolA"}
box.cfg{listen = 0}
test:ok(not box.info.listen:match("unix"), "box.info.listen")
local conn = net_box.connect(box.info.listen)
test:ok(conn:ping(), "conn:ping")
conn:close()
box.cfg{listen = ""}

os.exit(test:check() and 0 or 1)
