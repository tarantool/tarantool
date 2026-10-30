#!/usr/bin/env tarantool

box.cfg{
    memtx_allocator = os.getenv("MEMTX_ALLOCATOR"),
    log = "tarantool.log",
}

local tap = require('tap')
local test = tap.test("errno")

local ok, test_run = pcall(require, 'test_run')
test_run = ok and test_run.new() or nil
-- Default value `memtx` was added to have ability to run a
-- `engine-tap` test w/o `test-run.py`, see commit
-- commit 3bd870261c462416c29226414fe0a2d79aba0c74
-- ('box, datetime: datetime comparison for indices').
local engine = test_run and test_run:get_cfg('engine') or
               (os.getenv('TEST_ENGINE') or 'memtx')

function test.engine(self)
    return engine
end

return test

