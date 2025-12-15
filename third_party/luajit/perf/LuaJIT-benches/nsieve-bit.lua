-- Benchmark to check the performance of bitwise arithmetics and
-- access to the array structure. This benchmark finds all prime
-- numbers in a given segment. This is the bit variation.
-- For the details see:
-- https://en.wikipedia.org/wiki/Sieve_of_Eratosthenes

local bench = require("bench").new(arg)

local bit = require("bit")
local band, bxor, rshift, rol = bit.band, bit.bxor, bit.rshift, bit.rol

local function nsieve(p, m)
  local count = 0
  for i=0,rshift(m, 5) do p[i] = -1 end
  for i=2,m do
    if band(rshift(p[rshift(i, 5)], i), 1) ~= 0 then
      count = count + 1
      for j=i+i,m,i do
	local jx = rshift(j, 5)
	p[jx] = band(p[jx], rol(-2, j))
      end
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
  name = "nsieve_bit",
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
