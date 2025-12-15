local tap = require('tap')
local test = tap.test('bc-jit-unpatching'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

local utils = require('utils')
-- Function with up-recursion.
local function f(n)
  return n < 2 and n or f(n - 1) + f(n - 2)
end

local ret1bc = 'RET1%s*1%s*2'
-- Check that this bytecode still persists.
assert(utils.frontend.hasbc(load(string.dump(f)), ret1bc))

jit.opt.start('hotloop=1', 'hotexit=1')
-- Compile function to get JLOOP bytecode in recursion.
f(5)

test:ok(utils.frontend.hasbc(load(string.dump(f)), ret1bc),
        'bytecode unpatching is OK')

test:done(true)
