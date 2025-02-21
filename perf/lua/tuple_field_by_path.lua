local benchmark = require('benchmark')
local clock = require('clock')
local decimal = require('decimal')

local USAGE = [[
 This benchmark measures the performance of accessing tuple fields by their
 names referenced in the tuple format.
]]

local params = benchmark.argparse(arg, {}, USAGE)

local bench = benchmark.new(params)

local CYCLES = 3e8
local tuple_format = {
    {'f_number',  'number'},
    {'f_string',  'string'},
    {'f_decimal', 'decimal'},
}
local decimal_value = decimal.new(2)
local tuple_content = {1, 'a', decimal_value}
local tuple = box.tuple.new(tuple_content, {format = tuple_format})

local function payload(field_name, expected_value)
    local result
    for _ = 1, CYCLES do
        result = tuple[field_name]
    end
    -- Sanity check.
    assert(result == expected_value, 'correct field value')
end

local function run_test(field_name, field_type, expected_value)
    local testname = field_type
    -- XXX: This part is debatable, since the performance of the
    -- root trace is always better than the performance of the
    -- side trace. Without trace flushing, the order of the fields
    -- matters. OTOH, in the real world, we can't guarantee what
    -- field will be accessed first or what is the ratio of the
    -- field types accessing.
    jit.flush()

    local real_time = clock.time()
    local cpu_time = clock.proc()
    payload(field_name, expected_value)
    local real_delta = clock.time() - real_time
    local cpu_delta = clock.proc() - cpu_time

    bench:add_result(testname, {
        real_time = real_delta,
        cpu_time = cpu_delta,
        items = CYCLES,
    })
end

for fieldno, field_format in ipairs(tuple_format) do
    run_test(field_format[1], field_format[2], tuple_content[fieldno])
end

bench:dump_results()
