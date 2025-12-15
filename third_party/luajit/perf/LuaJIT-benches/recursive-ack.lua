-- The benchmark to check the performance of recursive calls.
-- Calculates the Ackermann function.
-- For the details see:
-- https://mathworld.wolfram.com/AckermannFunction.html

local bench = require("bench").new(arg)

local function Ack(m, n)
  if m == 0 then return n+1 end
  if n == 0 then return Ack(m-1, 1) end
  return Ack(m-1, (Ack(m, n-1))) -- The parentheses are deliberate.
end

local N = tonumber(arg and arg[1]) or 10

bench:add({
  name = "recursive_ack",
  -- Sum of calls for the function RA(3, N).
  items = 128 * ((4 ^ N - 1) / 3) - 40 * (2 ^ N - 1) + 3 * N + 15,
  payload = function()
    return Ack(3, N)
  end,
  checker = function(res)
    return res == 2 ^ (N + 3) - 3
  end,
})

bench:run_and_report()
