-- The benchmark to check the performance of multiple inner loops
-- with arithmetic operations. Calculates the Mandelbrot Set on a
-- bitmap and dumps output in the portable bitmap format.
-- For the details see:
-- https://benchmarksgame-team.pages.debian.net/benchmarksgame/description/mandelbrot.html

local bench = require("bench").new(arg)

local N = tonumber(arg and arg[1]) or 5000

local function payload()
  -- These functions must not be an upvalue but the stack slot.
  local N = N
  local write, char, unpack = io.write, string.char, unpack
  local M, ba, bb, buf = 2 / N, 2 ^ (N % 8 + 1) - 1, 2 ^ (8 - N % 8), {}
  write("P4\n", N, " ", N, "\n")
  for y = 0, N - 1 do
    local Ci, b, p = y * M - 1, 1, 0
    for x = 0, N - 1 do
      local Cr = x * M - 1.5
      local Zr, Zi, Zrq, Ziq = Cr, Ci, Cr * Cr, Ci * Ci
      b = b + b
      for i = 1, 49 do
        Zi = Zr * Zi * 2 + Ci
        Zr = Zrq - Ziq + Cr
        Ziq = Zi * Zi
        Zrq = Zr * Zr
        if Zrq + Ziq > 4.0 then b = b + 1; break; end
      end
      if b >= 256 then p = p + 1; buf[p] = 511 - b; b = 1; end
    end
    if b ~= 1 then p = p + 1; buf[p] = (ba - b) * bb; end
    write(char(unpack(buf, 1, p)))
  end
end

local stdout = io.output()

bench:add({
  name = "mandelbrot",
  items = N,
  -- XXX: This is inconvenient to have the binary file in the
  -- repository for the comparison. If the check is needed run,
  -- the payload manually.
  skip_check = true,
  setup = function()
    io.output("/dev/null")
  end,
  teardown = function()
    io.output(stdout)
  end,
  payload = payload,
})

bench:run_and_report()
