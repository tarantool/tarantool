local tap = require('tap')
local test = tap.test('lj-918-fma-optimization'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(3)

local function jit_opt_is_on(flag)
  for _, opt in ipairs({jit.status()}) do
    if opt == flag then
      return true
    end
  end
  return false
end

test:ok(not jit_opt_is_on('fma'), 'FMA is disabled by default')

local ok, _ = pcall(jit.opt.start, '+fma')

test:ok(ok, 'fma flag is recognized')

test:ok(jit_opt_is_on('fma'), 'FMA is enabled after jit.opt.start()')

test:done(true)
