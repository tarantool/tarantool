local json = require('cjson')

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
-- luacheck: push no max comment line length
--
-- [1]: https://docs.influxdata.com/influxdb/v2/reference/syntax/line-protocol/
--
-- luacheck: pop
--
-- The script takes 2 command line arguments:
-- | luajit aggregate.lua output_file [input_dir]
-- If `input_dir` isn't given, it uses the current directory by
-- default.
-- The script requires the `git` command or specified
-- `PERF_COMMIT`, `PERF_BRANCH` environment variables. Also, it
-- requires the `cjson` module.

local output = assert(arg[1], 'Output file is required as the first argument')
local input_dir = arg[2] or '.'

local out_fh = assert(io.open(output, 'w+'))

local function exec(cmd)
  return io.popen(cmd):read('*all'):gsub('%s+$', '')
end

local commit = os.getenv('PERF_COMMIT') or exec('git rev-parse --short HEAD')
assert(commit, 'can not determine the commit')

local branch = os.getenv('PERF_BRANCH') or
  exec('git rev-parse --abbrev-ref HEAD')
assert(branch, 'can not determine the branch')

-- Not very robust, but OK for our needs.
local function listdir(path)
  local handle = io.popen('ls -1 ' .. path)

  local files = {}
  for file in handle:lines() do
    table.insert(files, file)
  end

  return files
end

local tag_set = {branch = branch}

local function table_plain_copy(src)
  local dst = {}
  for k, v in pairs(src) do
    dst[k] = v
  end
  return dst
end

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

local function influx_kv(tab)
  local kv_string = {}
  for k, v in pairs(tab) do
    table.insert(kv_string, ('%s=%s'):format(k, v))
  end
  return table.concat(kv_string, ',')
end

local time = os.time()
local function influx_line(measurement, tags, fields)
  return ('%s,%s %s %d\n'):format(measurement, influx_kv(tags),
          influx_kv(fields), time)
end

for _, suite_name in pairs(listdir(input_dir)) do
  -- May list the report file, but will be ignored by the
  -- condition below.
  local suite_dir = ('%s/%s'):format(input_dir, suite_name)
  for _, file in pairs(listdir(suite_dir)) do
    -- Skip files in which we are not interested.
    if not file:match('%.json$') then goto continue end

    local data = read_all(('%s/%s'):format(suite_dir, file))
    local bench_name = file:match('([^/]+)%.json')
    local bench_data = json.decode(data)
    local benchmarks = bench_data.benchmarks
    local arch = bench_data.context.arch
    local gc64 = bench_data.context.gc64
    local jit = bench_data.context.jit

    for _, bench in ipairs(benchmarks) do
      local full_tag_set = table_plain_copy(tag_set)
      full_tag_set.name = bench.name
      full_tag_set.suite = suite_name
      full_tag_set.arch = arch
      full_tag_set.gc64 = gc64
      full_tag_set.jit = jit

      -- Save the commit as a field, since we don't want to filter
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
end

out_fh:close()
