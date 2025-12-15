local tap = require('tap')
local test = tap.test('gh-6976-narrowing-of-unary-minus'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(2)

jit.opt.start('hotloop=1')

local function check(routine)
  jit.off()
  jit.flush()
  local interp_res = routine()
  jit.on()
  local jit_res = routine()

  for i = 1, #interp_res do
    if interp_res[i] ~= jit_res[i] then
      return false
    end
  end

  return true
end

test:ok(check(function()
  -- We use `table.new()` here to avoid trace
  -- exits due to table rehashing.
  local res = require('table.new')(3, 0)
  for _ = 1, 3 do
    local zero = 0
    zero = -zero
    -- There is no difference between 0 and -0 from
    -- arithmetic perspective, unless you try to divide
    -- something by them.
    -- `1 / 0 = inf` and `1 / -0 = -inf`
    table.insert(res, 1 / zero)
  end
  return res
end), 'incorrect recording for zero')

test:ok(check(function()
  -- See the comment about `table.new()` above.
  local res = require('table.new')(3, 0)
  for i = 2, 0, -1 do
    table.insert(res, 1 / -i)
  end
  return res
end), 'assertion guard fail')

test:done(true)
