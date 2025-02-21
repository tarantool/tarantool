local benchmark = require('benchmark')
local clock = require('clock')
local decimal = require('decimal')

local USAGE = [[
 This benchmark measures the performance of accessing tuple fields by their
 names referenced in the tuple format.
]]

local params = benchmark.argparse(arg, {}, USAGE)

local bench = benchmark.new(params)

-- Some significant amount of time to run (each test >30 seconds).
local CYCLES = 3e8

local decimal_value = decimal.new(1)

local tuple_formats = {
    {'field', 'number'},
    {'field', 'string'},
    -- Use the decimal as one of the custom MsgPack datatypes with
    -- cdata.
    {'field', 'decimal'},
}

local tuple_content = {1, '1', decimal_value}

-- Use different tuples to always decode the first field in the
-- msgpack. So, there is no parasite work to skip the irrelevant
-- tuple fields.
local tuples = {}

for i = 1, #tuple_formats do
    table.insert(tuples, box.tuple.new(
        {tuple_content[i]}, {format = {tuple_formats[i]}}
    ))
end

local function payload(tuple, expected_value)
    local result
    for _ = 1, CYCLES do
        result = tuple.field
    end
    -- Sanity check.
    assert(result == expected_value, 'correct field value')
end

local function run_test(tuple, field_type, expected_value)
    local testname = field_type
    -- XXX: This part is debatable, since the performance of the
    -- root trace is always better than the performance of the
    -- side trace. Without trace flushing, the order of benchmarks
    -- (each for its own field type) matters. OTOH, in the real
    -- world, we can't guarantee what field will be accessed first
    -- or what is the ratio of the field types accessing.
    jit.flush()

    local real_time = clock.time()
    local cpu_time = clock.proc()
    payload(tuple, expected_value)
    local real_delta = clock.time() - real_time
    local cpu_delta = clock.proc() - cpu_time

    bench:add_result(testname, {
        real_time = real_delta,
        cpu_time = cpu_delta,
        items = CYCLES,
    })
end

for i = 1, #tuple_formats do
    run_test(tuples[i], tuple_formats[i][2], tuple_content[i])
end

bench:dump_results()
