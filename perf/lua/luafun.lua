local fun = require("fun")
local clock = require("clock")
local tarantool = require("tarantool")
local benchmark = require("benchmark")

local USAGE = [[
 This benchmark measures the performance of luafun module.
]]

local params = benchmark.argparse(arg, {}, USAGE)

local bench = benchmark.new(params)

local _, _, build_type =
    string.match(tarantool.build.target, "^(.+)-(.+)-(.+)$")
if build_type == "Debug" then
    print("WARNING: tarantool has built with enabled debug mode")
end
-- Fix the seed to stabilise the results.
math.randomseed(42)

--
-- Data for `drop_while` benchmark.
--

local DROP_WHILE_CYCLES = 10^3
local DROP_WHILE_N = 10^7

local function DROP_WHILE_FILTER(x)
    return x <= 50
end

-- A case when half of a table is dropped.
local DROP_WHILE_DATA = {}
for _ = 1, DROP_WHILE_N / 2 do
    local value = math.random(50)
    table.insert(DROP_WHILE_DATA, value)
end
for _ = 1, DROP_WHILE_N / 2 do
    local value = math.random(51, 100)
    table.insert(DROP_WHILE_DATA, value)
end

-- A case when the whole table is dropped.
local DROP_WHILE_DROP_ALL_DATA = {}
for _ = 1, DROP_WHILE_N do
    local value = math.random(50)
    table.insert(DROP_WHILE_DROP_ALL_DATA, value)
end

-- A case when no values are dropped.
local DROP_WHILE_DROP_NONE_DATA = {}
for _ = 1, DROP_WHILE_N do
    local value = math.random(51, 100)
    table.insert(DROP_WHILE_DROP_NONE_DATA, value)
end

-- Parametrised drop_while benchmark function:
-- 1. `test.cycles` - how many times to repeat the iteration;
-- 2. `test.data` - dataset to iterate over.
-- Pre-defined filter function is used.
local function DROP_WHILE_BENCHMARK(test)
    local acc = 0
    for _ = 1, test.cycles do
        for _, i in fun.iter(test.data):drop_while(DROP_WHILE_FILTER) do
            acc = acc + i
        end
    end
end

--
-- Benchmarks themselves.
-- Each benchmark must have `cycles` and `items` fields meaning
-- how many iterations are made and how many items are processed
-- by one iteration.
--

local tests = {
    {
        name = "drop_while",
        items = DROP_WHILE_N,
        cycles = DROP_WHILE_CYCLES,
        data = DROP_WHILE_DATA,
        payload = DROP_WHILE_BENCHMARK,
    }, {
        name = "drop_while.drop_all",
        items = DROP_WHILE_N,
        cycles = DROP_WHILE_CYCLES,
        data = DROP_WHILE_DROP_ALL_DATA,
        payload = DROP_WHILE_BENCHMARK,
    }, {
        name = "drop_while.drop_none",
        items = DROP_WHILE_N,
        cycles = DROP_WHILE_CYCLES,
        data = DROP_WHILE_DROP_NONE_DATA,
        payload = DROP_WHILE_BENCHMARK,
    }
}

local function run_test(test)
    local real_time = clock.time()
    local cpu_time = clock.proc()
    test.payload(test)
    local real_delta = clock.time() - real_time
    local cpu_delta = clock.proc() - cpu_time
    bench:add_result(test.name, {
        real_time = real_delta / test.cycles,
        cpu_time = cpu_delta / test.cycles,
        items = test.items,
    })
end

for _, test in ipairs(tests) do
    run_test(test)
end

bench:dump_results()
