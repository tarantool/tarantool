-- A benchmark of Tarantool serializers. A serializer is expected to
-- provide encode() and decode() functions.

local benchmark = require('benchmark')
local clock = require('clock')
local datetime = require('datetime')
local decimal = require('decimal')
local msgpack = require('msgpack')
local uuid = require('uuid')

local USAGE = [[
 This benchmark measures the performance of encoding and decoding of
 various data types with the Lua MsgPack serializers.
]]

local params = benchmark.argparse(arg, {}, USAGE)
local bench = benchmark.new(params)

local function array_of_ints(count)
    local res = table.new(count, 0)
    for i = 1, count do
        res[i] = i
    end
    return res
end

-- A heterogeneous array: a single call exercises several encoder and
-- decoder branches, unlike the homogeneous samples below.
local function mixed_array(count)
    local pattern = {1, 1, 'x', 1.5, true, 'y'}
    local res = table.new(count, 0)
    for i = 1, count do
        res[i] = pattern[(i - 1) % #pattern + 1]
    end
    return res
end

-- Sparse collections with box.NULL "holes". The __serialize hint makes
-- msgpack and msgpackffi treat the tables as arrays/maps identically.
-- Plain nil holes would be invisible to pairs() and msgpackffi would
-- stop counting an array at the first hole.
local function sparse_array(count)
    local res = table.new(count, 0)
    for i = 1, count do
        res[i] = i % 4 == 0 and i or box.NULL
    end
    return setmetatable(res, {__serialize = 'seq'})
end

local function sparse_map(count)
    local res = table.new(0, count)
    for i = 1, count do
        res['k' .. i] = i % 4 == 0 and i or box.NULL
    end
    return setmetatable(res, {__serialize = 'map'})
end

local function nested(depth)
    local res = 1
    for _ = 1, depth do
        res = {res}
    end
    return res
end

-- A representative subset of data types accepted by the serializers.
-- The first one is the sample from the issue. Not the whole set of
-- types is used to keep the benchmark fast and its results stable.
--
-- An optional third element divides the cycle count for expensive
-- samples.
local samples = {
    {'array_int_100', array_of_ints(100)},
    {'array_int_3', {1, 2, 3}},
    {'array_mixed_100', mixed_array(100)},
    {'array_sparse_100', sparse_array(100)},
    {'array_str_3', {'foo', 'bar', 'baz'}},
    {'bool', true},
    {'datetime', datetime.new()},
    {'decimal', decimal.new('1234567891234567890.0987654321')},
    {'double', 3.1415926535898},
    {'map_3', {k1 = 'v1', k2 = 'v2', k3 = 'v3'}},
    {'map_sparse_100', sparse_map(100)},
    {'nested_4', {1, {2, {3, {4}}}}},
    -- Worst-case recursion: `encode_max_depth - 1` nested tables.
    {'nested_encode_max_depth', nested(msgpack.cfg.encode_max_depth - 1), 100},
    {'string_100', string.rep('x', 100)},
    {'string_10k', string.rep('x', 10 * 1024), 100},
    {'string_1m', string.rep('x', 1024 * 1024), 10000},
    {'string_utf8', string.rep('ю', 50)}, -- 100 bytes, like 'string'.
    {'uint64', 18446744073709551615ULL},
    {'uuid', uuid.new()},
}

-- The JIT is measured both enabled and disabled, since some
-- serializers (msgpackffi) are expected to benefit from the JIT
-- compilation, but their hot loops appeared to be not compiled.
local jit_modes = {
    {name = 'jit_on', cycles = 10^6, enable = true},
    {name = 'jit_off', cycles = 10^5, enable = false},
}

local function measure(name, cycles, func, arg)
    -- Drop the traces from the previous subtest, so the measurement
    -- does not depend on the order of the subtests. This is intentional:
    -- every subtest starts with a fresh trace and the results do not
    -- model trace reuse across samples. Flush first: this may make
    -- objects referenced only by the flushed traces garbage, which are
    -- then collected by the following GC call.
    jit.flush()
    collectgarbage('collect')
    local real_time = clock.time()
    local cpu_time = clock.proc()
    local result
    for _ = 1, cycles do
        result = func(arg)
    end
    -- Keep the result observable: otherwise the JIT may eliminate the
    -- measured call as dead code, especially for small decode payloads.
    assert(result ~= nil)
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

-- @param serializers table a list of serializers to benchmark
local function run(serializers)
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
                local divisor = sample[3] or 1
                local cycles = math.floor(jit_mode.cycles / divisor)
                measure(('%s.encode.%s.%s'):format(
                            serializer.name, jit_mode.name, datatype),
                        cycles, serializer.encode, value)
                local encoded = serializer.encode(value)
                measure(('%s.decode.%s.%s'):format(
                            serializer.name, jit_mode.name, datatype),
                        cycles, serializer.decode, encoded)
            end
        end
    end
    jit.on()
end

-- To add a new serializer, append its module name (or a table with
-- 'name', 'encode' and 'decode' fields) to this list.
local serializers = {
    'msgpack',
    'msgpackffi',
}

run(serializers)

bench:dump_results()
