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
-- [1]: https://docs.influxdata.com/influxdb/v1/write_protocols/line_protocol_tutorial/

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
local out_fh = assert(io.open(output, 'w'))

local function exec(cmd)
    return io.popen(cmd):read('*all'):gsub('^%s+', ''):gsub('%s+$', '')
end

local commit = exec('git rev-parse --short HEAD')
assert(commit, 'can not determine the commit')

local branch = exec('git rev-parse --abbrev-ref HEAD')
assert(branch, 'can not determine the branch')

local tag_set = ('branch=%s'):format(branch)

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

for _, file in pairs(fio.listdir(input_dir)) do
    -- Skip files in which we are not interested.
    if not file:match('%.json$') then goto continue end

    local data = read_all(('%s/%s'):format(input_dir, file))
    local bench_name = fio.basename(file, '.json')
    local benchmarks = json.decode(data).benchmarks

    for _, bench in ipairs(benchmarks) do
        local full_tag_set = tag_set .. (',name=%s'):format(bench.name)

        -- Save commit as a field, since we don't want to filter
        -- benchmarks by the commit (one point of data).
        local field_set = {('commit="%s"'):format(commit)}

        for _, field in ipairs(REPORTED_FIELDS) do
            -- XXX: The stub for the `small` benchmark.
            if bench[field] then
                table.insert(field_set, ('%s=%s'):format(field, bench[field]))
            end
        end

        local line = ('%s,%s %s %d\n'):format(
            bench_name, full_tag_set, table.concat(field_set, ','), time
        )
        out_fh:write(line)
    end
    ::continue::
end

out_fh:close()
