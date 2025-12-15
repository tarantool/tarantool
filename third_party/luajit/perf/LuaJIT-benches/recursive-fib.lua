-- The benchmark to check the performance of recursive calls.
-- Calculates the Fibonacci values recursively.
-- For the details see:
-- http://mathworld.wolfram.com/FibonacciNumber.html

local bench = require("bench").new(arg)

local function fib(n)
  if n < 2 then return 1 end
  return fib(n-2) + fib(n-1)
end

local n = tonumber(arg[1]) or 40

local benchmark
benchmark = {
  name = "recursive_fib",
  checker = function(res)
    local km1, k = 1, 1
    for i = 2, n do
      local tmp = k + km1
      km1 = k
      k = tmp
    end
    return k == res
  end,
  payload = function()
    local res = fib(n)
    -- Number of calls.
    benchmark.items = res * 2 - 1
    return res
  end,
}

bench:add(benchmark)
bench:run_and_report()
