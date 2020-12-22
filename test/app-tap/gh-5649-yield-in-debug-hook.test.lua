#!/usr/bin/env tarantool

local fiber = require('fiber')
local tap = require('tap')
local test = tap.test('yield-in-debug-hook')

test:plan(2)

-- Test that HOOK_ACTIVE is not enough to panic and
-- fiber still can use general hooks at switches.
fiber.create(function()
  local old_hook, mask, count = debug.gethook()
  debug.sethook(function()
    fiber.yield()
  end, 'c')
  -- All ok if panic doesn't occure.
  -- Yield before hook is set back.
  debug.sethook(old_hook, mask, count)
  test:ok(true)
end)
-- Return to the second fiber.
fiber.yield()
test:ok(true)

os.exit(test:check() and 0 or 1)
