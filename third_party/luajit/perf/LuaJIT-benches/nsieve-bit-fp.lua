-- Benchmark to check the performance of FP arithmetics and
-- access to the array structure. This benchmark finds all prime
-- numbers in a given segment. This is the FP benchmark that
-- models the bit variation behaviour.
-- For the details see:
-- https://en.wikipedia.org/wiki/Sieve_of_Eratosthenes

local bench = require("bench").new(arg)

local floor, ceil = math.floor, math.ceil

local precision = 50 -- Maximum precision of lua_Number (minus safety margin).
local onebits = (2^precision)-1

local function nsieve(p, m)
  local cm = ceil(m/precision)
  do local onebits = onebits; for i=0,cm do p[i] = onebits end end
  local count, idx, bit = 0, 2, 2
  for i=2,m do
    local r = p[idx] / bit
    if r - floor(r) >= 0.5 then -- Bit set?
      local kidx, kbit = idx, bit
      for k=i+i,m,i do
        kidx = kidx + i
        while kidx >= cm do kidx = kidx - cm; kbit = kbit + kbit end
        local x = p[kidx]
        local r = x / kbit
        if r - floor(r) >= 0.5 then p[kidx] = x - kbit*0.5 end -- Clear bit.
      end
      count = count + 1
    end
    idx = idx + 1
    if idx >= cm then idx = 0; bit = bit + bit end
  end
  return count
end

local DEFAULT_N = 12
local N = tonumber(arg and arg[1]) or DEFAULT_N
if N < 2 then N = 2 end
local primes = {}

local benchmark
benchmark = {
  name = "nsieve_bit_fp",
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
