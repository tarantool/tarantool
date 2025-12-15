-- Benchmark to check the performance of working with strings and
-- output to the file. It generates DNA sequences by copying or
-- weighted random selection.
-- For details see:
-- https://benchmarksgame-team.pages.debian.net/benchmarksgame/description/fasta.html

local bench = require("bench").new(arg)

local stdout = io.output()

local benchmark
benchmark = {
  name = "fasta",
  -- XXX: The result file may take up to 278 Mb for the default
  -- settings. To check the correctness of the script, run it as
  -- is from the console.
  skip_check = true,
  setup = function()
    io.output("/dev/null")
  end,
  payload = function()
    -- Run the benchmark as is from the file.
    local items = require("fasta")
    -- Remove it from the cache to be sure the benchmark will run
    -- at the next iteration.
    package.loaded["fasta"] = nil
    benchmark.items = items
  end,
  teardown = function()
    io.output(stdout)
  end,
}

bench:add(benchmark)
bench:run_and_report()
