-- The benchmark to check the performance of FP arithmetic and
-- math functions. Calculates the partial sums of several series
-- in the single loop.

local bench = require("bench").new(arg)

local DEFAULT_N = 1e7
local n = tonumber(arg[1]) or DEFAULT_N

bench:add({
  name = "partialsums",
  payload = function()
    local a1, a2, a3, a4, a5, a6, a7, a8, a9, alt = 1, 0, 0, 0, 0, 0, 0, 0, 0, 1
    local sqrt, sin, cos = math.sqrt, math.sin, math.cos
    for k = 1, n do
      local k2, sk, ck = k * k, sin(k), cos(k)
      local k3 = k2 * k
      a1 = a1 + (2 / 3) ^ k
      a2 = a2 + 1 / sqrt(k)
      a3 = a3 + 1 / (k2 + k)
      a4 = a4 + 1 / (k3 * sk * sk)
      a5 = a5 + 1 / (k3 * ck * ck)
      a6 = a6 + 1 / k
      a7 = a7 + 1 / k2
      a8 = a8 + alt / k
      a9 = a9 + alt / (k + k - 1)
      alt = -alt
    end
    return {a1, a2, a3, a4, a5, a6, a7, a8, a9}
  end,
  checker = function(a)
    if n == DEFAULT_N then
      assert(a[1] == 2.99999999999999866773)
      assert(a[2] == 6323.09512394020111969439)
      assert(a[3] == 0.99999989999981531152)
      assert(a[4] == 30.31454593111029183206)
      assert(a[5] == 42.99523427973661426904)
      assert(a[6] == 16.69531136585727182364)
      assert(a[7] == 1.64493396684725956547)
      assert(a[8] == 0.69314713056010635039)
      assert(a[9] == 0.78539813839744787582)
    end
    return true
  end,
  items = n,
})

bench:run_and_report()
