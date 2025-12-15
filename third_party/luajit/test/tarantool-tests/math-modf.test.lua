local tap = require('tap')
local tnew = require('table.new')
local test = tap.test('math-modf'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

local function isnan(x)
    return x ~= x
end

local function array_is_consistent(res)
  for i = 1, #res - 1 do
    if res[i] ~= res[i + 1] and not (isnan(res[i]) and isnan(res[i + 1])) then
      return false
    end
  end
  return true
end

jit.opt.start('hotloop=1')

local modf = math.modf
local inf = math.huge

local r1 = tnew(4, 0)
local r2 = tnew(4, 0)

for i = 1, 3 do
  r1[i], r2[i] = modf(inf)
end

test:ok(array_is_consistent(r1) and array_is_consistent(r2), 'wrong modf')

test:done(true)
