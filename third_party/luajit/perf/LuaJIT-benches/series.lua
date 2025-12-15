-- The benchmark to check the performance of FP arithmetics, power
-- operation, and trigonometrical functions. Calculates the
-- integrals of sin/cos functions.

local bench = require("bench").new(arg)

local function integrate(x0, x1, nsteps, omegan, f)
  local x, dx = x0, (x1-x0)/nsteps
  local rvalue = ((x0+1)^x0 * f(omegan*x0)) / 2
  for i=3,nsteps do
    x = x + dx
    rvalue = rvalue + (x+1)^x * f(omegan*x)
  end
  return (rvalue + ((x1+1)^x1 * f(omegan*x1)) / 2) * dx
end

local function series(n)
  local sin, cos = math.sin, math.cos
  local omega = math.pi
  local t = {}

  t[1] = integrate(0, 2, 1000, 0, function() return 1 end) / 2
  t[2] = 0

  for i=2,n do
    t[2*i-1] = integrate(0, 2, 1000, omega*i, cos)
    t[2*i] = integrate(0, 2, 1000, omega*i, sin)
  end

  return t
end

local n = tonumber(arg and arg[1]) or 10000

bench:add({
  name = "series",
  checker = function(res)
    return math.abs(res[1] - 2.87295) < 0.00001
  end,
  payload = function()
    return series(n)
  end,
  items = 2 * n - 1,
})

bench:run_and_report()
