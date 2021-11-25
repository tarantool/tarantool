#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('invalid-tuple-size-max')
test:plan(4)

box.cfg{}

local _, err
_, err = pcall(box.cfg, {memtx_max_tuple_size = 0})
test:is(tostring(err), "Incorrect value for option 'memtx_max_tuple_size': " ..
		       "must be greater than 0", "error message")
_, err = pcall(box.cfg, {memtx_max_tuple_size = -1})
test:is(tostring(err), "Incorrect value for option 'memtx_max_tuple_size': " ..
		       "must be greater than 0", "error message")
_, err = pcall(box.cfg, {vinyl_max_tuple_size = 0})
test:is(tostring(err), "Incorrect value for option 'vinyl_max_tuple_size': " ..
		       "must be greater than 0", "error message")
_, err = pcall(box.cfg, {vinyl_max_tuple_size = -1})
test:is(tostring(err), "Incorrect value for option 'vinyl_max_tuple_size': " ..
		       "must be greater than 0", "error message")

os.exit(test:check() and 0 or 1)
