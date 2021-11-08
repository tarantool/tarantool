#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('gh-6198-max-cnt-of-tuple-fields-insert-overflow-err-msg')

test:plan(1)

box.cfg{}

local t = box.tuple.new({1}):update({{'=', box.schema.FIELD_MAX, 1}})
local expected_err_msg = 'Tuple field count limit reached: see ' ..
                         'box.schema.FIELD_MAX'
local ok, observed_err_msg = pcall(t.update, t, {{'!', #t, 1}})
test:is_deeply({ok, tostring(observed_err_msg)}, {false, expected_err_msg},
               'unable to insert into a tuple which size equals to ' ..
               'box.schema.FIELD_MAX')

os.exit(test:check() and 0 or 1)
