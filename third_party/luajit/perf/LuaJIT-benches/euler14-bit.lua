-- The benchmark to check the performance of bitwise operations.
-- It finds the longest Collatz sequence using bitwise arithmetic.
-- For the details see:
-- https://projecteuler.net/problem=14

local bench = require("bench").new(arg)

local bit = require("bit")
local bnot, bor, band = bit.bnot, bit.bor, bit.band
local shl, shr = bit.lshift, bit.rshift

local DEFAULT_N = 2e7
local N = tonumber(arg and arg[1]) or DEFAULT_N
local drop_cache = arg and arg[2]

bench:add({
  name = "euler14_bit",
  payload = function()
    local cache, m, n = { 1 }, 1, 1
    if drop_cache then cache = nil end
    for i = 2, N do
      local j = i
      for len = 1, 1000000000 do
        j = bor(
          band(shr(j, 1), band(j, 1) - 1),
          band(shl(j, 1) + j + 1, bnot(band(j, 1) - 1))
        )
        if cache then
          local x = cache[j]
          if x then
            j = x + len
            break
          end
        elseif j == 1 then
          j = len + 1
          break
        end
      end
      if cache then cache[i] = j end
      if j > m then m, n = j, i end
    end
    return {n = n, m = m}
  end,
  checker = function(res)
    if N ~= DEFAULT_N then
      -- Test only for the default.
      return true
    else
      return res.n == 18064027 and res.m == 623
    end
  end,
  items = N,
})

bench:run_and_report()
