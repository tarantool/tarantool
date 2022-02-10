#!/usr/bin/env tarantool

box.cfg{
    log = "tarantool.log",
}

local tap = require('tap')
local test = tap.test("errno")

local ok, test_run = pcall(require, 'test_run')
test_run = ok and test_run.new() or nil
local engine = test_run ~= nil and test_run:get_cfg('engine') or 'memtx'

function test.engine(self)
    return engine
end

return test

