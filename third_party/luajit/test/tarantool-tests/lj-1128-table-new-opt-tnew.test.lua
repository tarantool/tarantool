local tap = require('tap')
local test = tap.test('lj-1128-table-new-opt-tnew'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
})

-- Test LuaJIT optimization when a call to the `lj_tab_new_ah()`
-- is replaced with the corresponding TNEW IR.
-- See also https://github.com/LuaJIT/LuaJIT/issues/1128.

local jparse = require('utils').jit.parse

-- API follows the semantics of `lua_createtable()`.
local table_new = require('table.new')

-- `hbits` for different `hsizes`, see <lj_tab.h> for details.
local HBITS = {
  [1] = 1,
  [3] = 2,
}

-- XXX: Avoid any other traces compilation due to hotcount
-- collisions for predictable results.
jit.off()
jit.flush()

test:plan(10)

jit.on()
jit.opt.start('hotloop=1')
jparse.start('i')

local anchor

for _ = 1, 4 do
  anchor = table_new(1, 1)
end

local traces = jparse.finish()
jit.off()

test:ok(type(anchor) == 'table', 'type check base result')
test:ok(traces[1]:has_ir(('TNEW.*#2.*#%d'):format(HBITS[1])), 'base IR value')

jit.flush()
jit.on()
-- XXX: Reset hotcounters.
jit.opt.start('hotloop=1')
jparse.start('i')

for _ = 1, 4 do
  anchor = table_new(0, 0)
end

traces = jparse.finish()
jit.off()

test:ok(type(anchor) == 'table', 'type check 0 asize, 0 hsize')
test:ok(traces[1]:has_ir('TNEW.*#0.*#0'), '0 asize, 0 hsize')

jit.flush()
jit.on()
-- XXX: Reset hotcounters.
jit.opt.start('hotloop=1')
jparse.start('i')

for _ = 1, 4 do
  anchor = table_new(0, 3)
end

traces = jparse.finish()
jit.off()

test:ok(type(anchor) == 'table', 'type check 3 hsize -> 2 hbits')
test:ok(traces[1]:has_ir(('TNEW.*#0.*#%d'):format(HBITS[3])),
        '3 hsize -> 2 hbits')

jit.flush()
jit.on()
-- XXX: Reset hotcounters.
jit.opt.start('hotloop=1')
jparse.start('i')

for _ = 1, 4 do
  anchor = table_new(-1, 0)
end

traces = jparse.finish()
jit.off()

test:ok(type(anchor) == 'table', 'type check negative asize')
test:ok(traces[1]:has_ir('TNEW.*#0.*#0'), 'negative asize')

jit.flush()
jit.on()
-- XXX: Reset hotcounters.
jit.opt.start('hotloop=1')
jparse.start('i')

for _ = 1, 4 do
  anchor = table_new(0xffff, 0)
end

traces = jparse.finish()
jit.off()

test:ok(type(anchor) == 'table', 'type check asize out of range')
-- Test that TNEW isn't emitted for `asize` bigger than the IR
-- operand width (>0x8000).
test:ok(not traces[1]:has_ir('TNEW'), 'asize out of range')

test:done(true)
