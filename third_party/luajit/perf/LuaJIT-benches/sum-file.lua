-- The benchmark to check the performance of reading lines from
-- stdin and sum the given numbers (the strings converted to
-- numbers by the VM automatically).

local bench = require("bench").new(arg)

-- XXX: The input file is generated from <SUMCOL_1.txt> by
-- repeating it 5000 times. The <SUMCOL_1.txt> contains 1000 lines
-- with the total sum of 500.
bench:add({
  name = "sum_file",
  payload = function()
    local sum = 0
    for line in io.lines() do
      sum = sum + line
    end
    -- Allow several iterations.
    io.stdin:seek("set", 0)
    return sum
  end,
  checker = function(res)
    -- Precomputed result.
    return res == 2500000
  end,
  -- Fixed size of the file.
  items = 5e6,
})

bench:run_and_report()
