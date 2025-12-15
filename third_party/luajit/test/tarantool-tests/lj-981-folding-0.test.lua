local tap = require('tap')
local test = tap.test('lj-981-folding-0'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
  ['Disabled on *BSD due to #4819'] = jit.os == 'BSD',
})

-- Test file to demonstrate LuaJIT misbehaviour on load forwarding
-- for -0 IR constant as table index.
-- See also, https://github.com/LuaJIT/LuaJIT/issues/981.

local jparse = require('utils').jit.parse

-- XXX: Avoid any other traces compilation due to hotcount
-- collisions for predictable results.
jit.off()
jit.flush()

jit.opt.start('hotloop=1')

test:plan(4)

jit.on()
jparse.start('i')
local result
local expected = 'result'
-- TNEW:
-- -0 isn't folded during parsing, so it will be set with KSHORT,
-- UNM bytecodes. See <src/lj_parse.c> and bytecode listing
-- for details.
-- Because of it, empty table is created via TNEW.
for _ = 1, 4 do
  result = ({[-0] = expected})[0]
end

local traces = jparse.finish()

jit.off()

-- Test that there is no any assertion failure.
test:ok(result == expected, 'TNEW and -0 folding')
-- Test that there is no NEWREF -0 IR.
test:ok(not traces[1]:has_ir('NEWREF.*-0'), '-0 is canonized for TNEW tab')

-- Reset traces.
jit.flush()

jit.on()
jparse.start('i')
-- TDUP:
-- Now just add a constant field for the table to use TDUP with
-- template table instead TNEW before -0 is set.
for _ = 1, 4 do
  result = ({[-0] = expected, [1] = 1})[0]
end

traces = jparse.finish()

-- Test that there is no any assertion failure.
test:ok(result == expected, 'TDUP and -0 folding')
-- Test that there is no NEWREF -0 IR.
test:ok(not traces[1]:has_ir('NEWREF.*-0'), '-0 is canonized for TDUP tab')

test:done(true)
