local benchmark = require('benchmark')
local serializer_bench = require('serializer_bench')

local USAGE = [[
 This benchmark measures the performance of encoding and decoding of
 various data types with the Lua MsgPack serializers.
]]

local params = benchmark.argparse(arg, {}, USAGE)
local bench = benchmark.new(params)

-- To add a new serializer, append its module name (or a table with
-- 'name', 'encode' and 'decode' fields) to this list.
local serializers = {
    'msgpack',
    'msgpackffi',
}

serializer_bench.run(bench, serializers)

bench:dump_results()
