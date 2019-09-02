#!/usr/bin/env tarantool
local fiber = require('fiber')
local tap = require('tap')
local test = tap.test("cfg")

test:plan(9)

test:is(type(box.ctl), "table", "box.ctl is available before box.cfg")
test:is(type(box.ctl.wait_ro), "function", "box.ctl.wait_ro is available")
test:is(type(box.ctl.wait_rw), "function", "box.ctl.wait_rw is available")

local f_ro = fiber.create(function() box.ctl.wait_ro() end)
local f_rw = fiber.create(function() box.ctl.wait_rw() end)

test:is(f_ro:status(), "suspended", "initially the server is neither read only nor read-write")
test:is(f_rw:status(), "suspended", "initially the server is neither read only nor read-write")

box.cfg{}

while f_ro:status() ~= "dead" do fiber.sleep(0.01) end
test:is(f_ro:status(), "dead", "initialized read-write mode. read-only waiter dies.")
while f_rw:status() ~= "dead" do fiber.sleep(0.01) end
test:is(f_rw:status(), "dead", "initialized read-write mode. read-write waiter dies.")
f_ro = fiber.create(function() box.ctl.wait_ro() end)
test:is(f_ro:status(), "suspended", "new read-only waiter is blocked")

box.cfg{read_only=true}

while f_ro:status() ~= "dead" do fiber.sleep(0.01) end
test:is(f_ro:status(), "dead", "entered read-only mode")

test:check()
os.exit(0)
