#!/usr/bin/env tarantool

-- File to aggregate the benchmark results from JSON files to the
-- format parsable by the InfluxDB line protocol [1]:
-- <measurement>,<tag_set> <field_set> <timestamp>
--
-- <tag_set> and <field_set> have the following format:
-- <key1>=<value1>,<key2>=<value2>
--
-- The reported tag set is a set of values that can be used for
-- filtering data (i.e., branch or benchmark name).
--
-- The script accepts the following parameters:
--
-- <input_dir> -- the directory from which the .json files are
--                taken.
-- <output>    -- the filename where the results are saved.
--
-- [1]: https://docs.influxdata.com/influxdb/v2/reference/syntax/line-protocol/

local json = require('json')
local fio = require('fio')

local params = require('internal.argparse').parse(arg, {
    {'input_dir', 'string'},
    {'output', 'string'},
})

local input_dir = params.input_dir
assert(input_dir and fio.path.is_dir(input_dir),
       'given input_dir is not a directory')

local output = params.output
local out_fh = assert(fio.open(output, {'O_WRONLY', 'O_CREAT', 'O_TRUNC'}))

local function exec(cmd)
    return io.popen(cmd):read('*all'):strip()
end

local commit = os.getenv('PERF_COMMIT') or exec('git rev-parse --short HEAD')
assert(commit, 'can not determine the commit')

local branch = os.getenv('PERF_BRANCH') or
    exec('git rev-parse --abbrev-ref HEAD')
assert(branch, 'can not determine the branch')

local tag_set = {branch = branch}

local function read_all(file)
    local fh = assert(io.open(file, 'rb'))
    local content = fh:read('*all')
    fh:close()
    return content
end

local REPORTED_FIELDS = {
    'cpu_time',
    'items_per_second',
    'iterations',
    'real_time',
}

local time = os.time()

local function influx_kv(tab)
    local kv_string = {}
    for k, v in pairs(tab) do
        table.insert(kv_string, ('%s=%s'):format(k, v))
    end
    return table.concat(kv_string, ',')
end

local function influx_line(measurement, tags, fields)
    return ('%s,%s %s %d\n'):format(measurement, influx_kv(tags),
            influx_kv(fields), time)
end

for _, file in pairs(fio.listdir(input_dir)) do
    -- Skip files in which we are not interested.
    if not file:match('%.json$') then goto continue end

    local data = read_all(('%s/%s'):format(input_dir, file))
    local bench_name = fio.basename(file, '.json')
    local benchmarks = json.decode(data).benchmarks

    for _, bench in ipairs(benchmarks) do
        local full_tag_set = table.deepcopy(tag_set)
        full_tag_set.name = bench.name

        -- Save commit as a field, since we don't want to filter
        -- benchmarks by the commit (one point of data).
        local field_set = {commit = ('"%s"'):format(commit)}

        for _, field in ipairs(REPORTED_FIELDS) do
            field_set[field] = bench[field]
        end

        local line = influx_line(bench_name, full_tag_set, field_set)
        out_fh:write(line)
    end
    ::continue::
end

out_fh:close()
