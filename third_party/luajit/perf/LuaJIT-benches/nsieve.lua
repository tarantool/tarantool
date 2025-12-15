-- Benchmark to check the performance of access to the array
-- structure in the tiny inner loops. This benchmark finds all the
-- prime numbers in a given segment. This is the most
-- straightforward implementation.
-- For the details see:
-- https://en.wikipedia.org/wiki/Sieve_of_Eratosthenes

local bench = require("bench").new(arg)

local function nsieve(p, m)
  for i=2,m do p[i] = true end
  local count = 0
  for i=2,m do
    if p[i] then
      for k=i+i,m,i do p[k] = false end
      count = count + 1
    end
  end
  return count
end

local DEFAULT_N = 12
local N = tonumber(arg and arg[1]) or DEFAULT_N
if N < 2 then N = 2 end
local primes = {}

local benchmark
benchmark = {
  name = "nsieve",
  payload = function()
    local res = {}
    local items = 0
    for i = 0, 2 do
      local m = (2 ^ (N - i)) * 10000
      items = items + m
      res[i] = nsieve(primes, m)
    end
    benchmark.items = items

    return res
  end,
  checker = function(res)
    if N == DEFAULT_N then
      assert(res[0] == 2488465)
      assert(res[1] == 1299069)
      assert(res[2] == 679461)
    end
    return true
  end,
}

bench:add(benchmark)
bench:run_and_report()
