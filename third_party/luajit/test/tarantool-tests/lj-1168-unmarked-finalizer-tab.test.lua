local tap = require('tap')

-- This test demonstrates LuaJIT's heap-use-after-free on
-- cleaning of resources during shutdown. The test simulates
-- "unloading" of the library, or removing some of its
-- functionality and then calls `collectgarbage`.
-- See https://github.com/LuaJIT/LuaJIT/issues/1168 for details.
local test = tap.test('lj-1168-unmarked-finalizer-tab')
test:plan(1)

local ffi = require('ffi')

ffi.gc = nil
collectgarbage()

test:ok(true, 'no heap use after free')

test:done(true)
