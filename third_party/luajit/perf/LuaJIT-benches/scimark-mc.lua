local bench = require("bench").new(arg)

local cycles = tonumber(arg and arg[1]) or 15e7

local benchmark
benchmark = {
  name = "scimark_mc",
  -- XXX: The description of tests for the function is too
  -- inconvenient.
  skip_check = true,
  payload = function()
    local flops = require("scimark_lib").MC()(cycles)
    benchmark.items = flops
  end,
}

bench:add(benchmark)

bench:run_and_report()
