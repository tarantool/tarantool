local tap = require('tap')
-- Test file to demonstrate LuaJIT's incorrect fusion across
-- `IR_NEWREF`.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1117.
local test = tap.test('lj-1117-fuse-across-newref'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local ffi = require('ffi')

test:plan(1)

-- Table with content.
local tab = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 42}
-- Use the alias to trick the code flow analysis.
local tab_alias = tab
local result_tab = {}

-- Need to start recording trace at the 16th iteration to avoid
-- rehashing of the `t` and `result_tab` before the `if`
-- condition below on the 32nd iteration. Also, the inner loop
-- isn't recorded this way. After rehashing in the NEWREF, the
-- fusion will use the wrong address, which leads to the dirty
-- reads visible (always, not flaky) under Valgrind with the
-- `--free-fill` option set.
jit.opt.start('hotloop=16')

-- The amount of iterations required for the rehashing of the
-- table.
for i = 1, 33 do
  -- ALOAD to be fused.
  local value = tab[16]
  -- NEWREF instruction.
  tab_alias[{}] = 100
  -- Need this CONV cast to trigger load fusion. See `asm_comp()`
  -- for the details. Before the patch, this fusion takes the
  -- incorrect address of the already deallocated array part of
  -- the table, which leads to the incorrect result.
  result_tab[i] = ffi.cast('int64_t', value)
  if i == 32 then
    -- Clear the array part.
    for j = 1, 15 do
      tab[j] = nil
    end
    -- Next rehash of the `tab`/`tab_alias` will dealloc the array
    -- part.
  end
end

test:samevalues(result_tab, 'no fusion across NEWREF')

test:done(true)
