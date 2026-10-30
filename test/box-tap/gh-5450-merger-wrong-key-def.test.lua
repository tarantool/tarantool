#!/usr/bin/env tarantool

local tap = require('tap')
local key_def_lib = require('key_def')
local merger = require('merger')

local test = tap.test('gh-5450-merger-wrong-key-def')
test:plan(1)

-- gh-5450: creating of merger leads to segfault, when passed
-- key_def is not usable to create a tuple format.
local key_def = key_def_lib.new({
    {fieldno = 6, type = 'string'},
    {fieldno = 6, type = 'unsigned'},
})
local exp_err = "Field 6 has type 'string' in one index, but type " ..
                "'unsigned' in another"
-- At the moment of writting the test, it is sufficient to pass an
-- empty sources table to trigger the attempt to create a tuple
-- format.
local sources = {}
local ok, err = pcall(merger.new, key_def, sources)
test:is_deeply({ok, tostring(err)}, {false, exp_err},
               'unable to create a tuple format')

os.exit(test:check() and 0 or 1)
