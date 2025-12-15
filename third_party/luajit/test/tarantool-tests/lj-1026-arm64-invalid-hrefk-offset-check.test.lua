local tap = require('tap')

-- Test file to demonstrate LuaJIT misbehaviour when assembling
-- HREFK instruction on arm64 with the huge offset.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1026.
local test = tap.test('lj-1026-arm64-invalid-hrefk-offset-check'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

-- The assertion fails since in HREFK we are checking the offset
-- from the hslots of the table of the `Node` structure itself
-- instead of its inner field `key` (with additional 8 bytes).
-- So to test this, we generate a big table with constant keys
-- and compile a trace for each HREFK possible.

local big_tab = {}
-- The map of the characters to generate constant string keys.
-- The offset of the node should be 4096 * 8. It takes at least
-- 1365 keys to hit this value. The maximum possible slots in the
-- hash part is 2048, so to fill it with the maximum density (with
-- the way below), we need 45 * 45 = 2025 keys.
local chars = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRS'
for c1 in chars:gmatch('.') do
  for c2 in chars:gmatch('.') do
    big_tab[c1 .. c2] = 1
  end
end

jit.opt.start('hotloop=1')

-- Generate bunch of traces.
for c1 in chars:gmatch('.') do
  for c2 in chars:gmatch('.') do
    loadstring([=[
      local t = ...
      for i = 1, 4 do
        -- HREFK generation.
        t[ ']=] .. c1 .. c2 .. [=[' ] = i
      end
    ]=])(big_tab)
  end
end

test:ok(true, 'no assertion failed')

test:done(true)
