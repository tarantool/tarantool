-- A reusable driver for benchmarking Tarantool serializers. A
-- serializer is expected to provide encode() and decode() functions.

local clock = require('clock')
local datetime = require('datetime')
local decimal = require('decimal')
local uuid = require('uuid')

local M = {}

local function array_of_ints(count)
    local res = {}
    for i = 1, count do
        res[i] = i
    end
    return res
end

-- A representative subset of data types accepted by the serializers.
-- The first one is the sample from the issue. Not the whole set of
-- types is used to keep the benchmark fast and its results stable.
M.samples = {
    {'array_int_100', array_of_ints(100)},
    {'array_int', {1, 2, 3}},
    {'array_str', {'foo', 'bar', 'baz'}},
    {'bool', true},
    {'datetime', datetime.new()},
    {'decimal', decimal.new('1234567891234567890.0987654321')},
    {'double', 3.1415926535898},
    {'map', {k1 = 'v1', k2 = 'v2', k3 = 'v3'}},
    {'nested', {1, {2, {3, {4}}}}},
    {'string', string.rep('x', 100)},
    {'uint64', 18446744073709551615ULL},
    {'uuid', uuid.new()},
}

-- The JIT is measured both enabled and disabled, since some
-- serializers (msgpackffi) are expected to benefit from the JIT
-- compilation, but their hot loops appeared to be not compiled.
M.jit_modes = {
    {name = 'jit_on', cycles = 10^5, enable = true},
    {name = 'jit_off', cycles = 10^4, enable = false},
}

local function measure(bench, name, cycles, func, arg)
    collectgarbage('collect')
    -- Drop the traces from the previous subtest, so the measurement
    -- does not depend on the order of the subtests.
    jit.flush()
    local real_time = clock.time()
    local cpu_time = clock.proc()
    for _ = 1, cycles do
        func(arg)
    end
    bench:add_result(name, {
        real_time = clock.time() - real_time,
        cpu_time = clock.proc() - cpu_time,
        items = cycles,
    })
end

-- A serializer is either a module name or a table with 'name',
-- 'encode' and 'decode' fields.
local function normalize(serializer)
    if type(serializer) == 'string' then
        local module = require(serializer)
        return {
            name = serializer,
            encode = module.encode,
            decode = module.decode,
        }
    end
    return serializer
end

-- @param bench table an object created by the 'benchmark' module
-- @param serializers table a list of serializers to benchmark
-- @param opts table optional 'samples' and 'jit_modes' overrides
function M.run(bench, serializers, opts)
    opts = opts or {}
    local samples = opts.samples or M.samples
    local jit_modes = opts.jit_modes or M.jit_modes
    for _, jit_mode in ipairs(jit_modes) do
        if jit_mode.enable then
            jit.on()
        else
            jit.off()
        end
        for _, serializer in ipairs(serializers) do
            serializer = normalize(serializer)
            for _, sample in ipairs(samples) do
                local datatype = sample[1]
                local value = sample[2]
                measure(bench, ('%s.encode.%s.%s'):format(
                            serializer.name, jit_mode.name, datatype),
                        jit_mode.cycles, serializer.encode, value)
                local encoded = serializer.encode(value)
                measure(bench, ('%s.decode.%s.%s'):format(
                            serializer.name, jit_mode.name, datatype),
                        jit_mode.cycles, serializer.decode, encoded)
            end
        end
    end
    jit.on()
end

return M
