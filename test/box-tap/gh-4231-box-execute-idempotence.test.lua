#!/usr/bin/env tarantool

--
-- gh-4231: box.execute should be an idempotent function meaning
-- its effect should be the same if the user chooses to save it
-- before explicit box.cfg{} invocation and use the saved version
-- afterwards.
--

local tap = require('tap')
local test = tap.test('gh-4231-box-execute-idempotence')
test:plan(3)

local box_load_and_execute = box.execute

-- box is not initialized after invalid box.cfg() call and
-- box.execute() should initialize it first.
pcall(box.cfg, {listen = 'invalid'})
local ok, res = pcall(box.execute, 'SELECT 1')
test:ok(ok, 'verify box.execute() after invalid box.cfg() call', {err = res})

-- Make test cases independent: if the box.execute() call above
-- fails, initialize box explicitly for the next test cases.
box.cfg{}

-- This call should be successful and should skip box
-- (re)initialization.
local ok, res = pcall(box_load_and_execute, 'SELECT 1')
test:ok(ok, 'verify box_load_and_execute after successful box.cfg() call',
        {err = res})

-- Just in case: verify usual box.execute() after
-- box_load_and_execute().
local ok, res = pcall(box.execute, 'SELECT 1')
test:ok(ok, 'verify box.execute() after box_load_and_execute()', {err = res})

os.exit(test:check() and 0 or 1)
