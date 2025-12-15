local tap = require('tap')
-- Test file to demonstrate LuaJIT's incorrect fusion across
-- `table.clear()`.
-- See also: https://github.com/LuaJIT/LuaJIT/issues/1117.
local test = tap.test('lj-1117-fuse-across-table-clear'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

local ffi = require('ffi')
local table_clear = require('table.clear')

test:plan(1)

local tab = {0}
local alias_tab = tab
local result_tab = {}

jit.opt.start('hotloop=1')

for i = 1, 4 do
  -- Load to be fused.
  local value = tab[1]
  -- Clear the alias table to trick the code flow analysis.
  table_clear(alias_tab)
  -- Need this cast to trigger load fusion. See `asm_comp()` for
  -- the details. Before the patch, this fusion takes the
  -- incorrect address of the already cleared table, which leads
  -- to the failure of the check below.
  result_tab[i] = ffi.cast('int64_t', value)
  -- Revive the value.
  tab[1] = 0
end

test:samevalues(result_tab, 'no fusion across table.clear')

test:done(true)
