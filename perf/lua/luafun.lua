local fun = require("fun")
local clock = require("clock")
local t = require("tarantool")
local benchmark = require("benchmark")

local USAGE = [[
 This benchmark measures the performance of luafun module.
]]

local params = benchmark.argparse(arg, {}, USAGE)

local bench = benchmark.new(params)

local _, _, build_type = string.match(t.build.target, "^(.+)-(.+)-(.+)$")
if build_type == "Debug" then
    print("WARNING: tarantool has built with enabled debug mode")
end

--
-- Data for `drop_while` benchmark.
--

local DROP_WHILE_CYCLES = 10^3
local DROP_WHILE_N = 10^7

local function DROP_WHILE_FILTER(x)
    return x <= 50
end

local DROP_WHILE_DATA = {}
for _ = 1, DROP_WHILE_N / 2 do
    local value = math.random(50)
    table.insert(DROP_WHILE_DATA, value)
end
for _ = 1, DROP_WHILE_N / 2 do
    local value = math.random(51, 100)
    table.insert(DROP_WHILE_DATA, value)
end

--
-- Benchmarks themselves.
--

local tests = {{
    name = "drop_while",
    items = DROP_WHILE_N,
    cycles = DROP_WHILE_CYCLES,
    payload = function()
        local acc = 0
        for _ = 1, DROP_WHILE_CYCLES do
            -- Random factor to disable any possible optimizations
            local factor = math.random(10)
            for _, i in fun.iter(t):drop_while(DROP_WHILE_FILTER) do
                acc = acc + i * factor
            end
        end
    end,
}}

local function run_test(testname, func, items, cycles)
    local real_time = clock.time()
    local cpu_time = clock.proc()
    func()
    local real_delta = clock.time() - real_time
    local cpu_delta = clock.proc() - cpu_time
    bench:add_result(testname, {
        real_time = real_delta / cycles,
        cpu_time = cpu_delta / cycles,
        items = items,
    })
end

for _, test in ipairs(tests) do
    run_test(test.name, test.payload, test.items, test.cycles)
end

bench:dump_results()
