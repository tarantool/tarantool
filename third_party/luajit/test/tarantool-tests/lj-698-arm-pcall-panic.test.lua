local tap = require('tap')

-- See also https://github.com/LuaJIT/LuaJIT/issues/698.
local test = tap.test('lj-418-arm-pcall-panic')
test:plan(1)

local ffi = require('ffi')
-- The test case below was taken from the LuaJIT-tests
-- suite (lib/ffi/ffi_callback.lua), and should be removed
-- after the integration of the mentioned suite.
local runner = ffi.cast("int (*)(int, int, int, int, int, int, int, int, int)",
                        function() error("test") end
                      )

local st = pcall(runner, 1, 1, 1, 1, 1, 1, 1, 1, 1)
test:ok(not st, 'error handling completed correctly')

test:done(true)
