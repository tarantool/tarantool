-- Usage:
--
-- local benchmark = require('benchmark')
-- local clock = require('clock')
--
-- local USAGE = 'tarantool mybench.lua [options]'
--
-- -- These options are parsed by the module by default:
-- -- output = 'string',
-- -- output_format = 'string',
-- local opts = benchmark.argparse(arg, {
--     <..your options..>
-- }, USAGE)
-- local bench = benchmark.new(opts)
--
-- local ITERATIONS = 10
--
-- local start_time = {
--     time = clock.time(),
--     proc = clock.proc(),
-- }
-- for _ = 1, ITERATIONS do
--     workload()
-- end
--
-- bench:add_result('subtest name', {
--     items = ITERATIONS,
--     real_time = clock.time() - start_time.time,
--     cpu_time = clock.proc() - start_time.proc,
-- })
--
-- bench:dump_results()

local json = require('json')
local fio = require('fio')
local argparse = require('internal.argparse')
local tarantool = require('tarantool')
local datetime = require('datetime')

local M = {}

local function format_report(bench)
    local output_format = bench.output_format
    local results = bench.results
    local report = ''
    if output_format == 'json' then
        -- The output should have the same format as the Google
        -- Benchmark JSON output format:
        -- https://github.com/google/benchmark/blob/main/docs/user_guide.md
        report = json.encode({
            benchmarks = results,
            context = bench.context,
        })
    else
        assert(output_format == 'console', 'unknown output format')
        for _, res in ipairs(results) do
            report = report .. ('%s %d rps\n'):format(res.name,
                                                      res.items_per_second)
        end
    end
    return report
end

local function dump_report(bench, text)
    local output = bench.output
    if output then
        local fh = assert(fio.open(output, {'O_WRONLY', 'O_CREAT', 'O_TRUNC'}))
        fh:write(text)
        fh:close()
    else
        io.stdout:write(text)
    end
end

local function add_result(bench, name, data)
    local items_per_second = math.floor(data.items / data.real_time)
    local result = {
        name = name,
        real_time = data.real_time,
        cpu_time = data.cpu_time,
        iterations = data.items,
        items_per_second = items_per_second,
        run_name = name,
        run_type = 'iteration',
        repetitions = 1,
        repetition_index = 1,
        threads = 1,
        time_unit = 's',
    }
    table.insert(bench.results, result)
    return result
end

local function dump_results(bench)
    dump_report(bench, format_report(bench))
end

local GENERAL_HELP = [[
 The supported general options list:

   help (same as -h) <boolean>       - print this message
   output <string>                   - filename to dump the benchmark results
   output_format <string, 'console'> - format (console, json) in which results
                                       are dumped

 Options can be used with '--', followed by the value if it's not a boolean
 option.

 There are a bunch of suggestions how to achieve the most stable results:
 https://github.com/tarantool/tarantool/wiki/Benchmarking
]]

function M.argparse(arg, argtable, custom_help)
    local benchname = fio.basename(debug.getinfo(2).short_src)
    local usageline = ('\n Usage: tarantool %s [options]\n\n'):format(benchname)
    argtable = argtable or {}
    table.insert(argtable, {'h', 'boolean'})
    table.insert(argtable, {'help', 'boolean'})
    table.insert(argtable, {'output', 'string'})
    table.insert(argtable, {'output_format', 'string'})
    local params = argparse.parse(arg, argtable)
    if params.h or params.help then
        local help_msg = usageline .. GENERAL_HELP
        if custom_help then
            help_msg = ('%s%s%s'):format(usageline, custom_help, GENERAL_HELP)
        end
        print(help_msg)
        os.exit(0)
    end
    return params
end

local function load_average()
    local path = '/proc/loadavg'
    local fh = assert(fio.open(path, {'O_RDONLY'}))
    local loadavg_buf = fh:read(1024)
    fh:close()
    -- Format is '0.89 0.66 0.80 2/1576 1203510'.
    local loadavg_re = '(%d+.%d+) (%d+.%d+) (%d+.%d+)'

    return { string.match(loadavg_buf, loadavg_re) }
end

-- The function return a ISO 8061 formatted datetime.
--
-- Google Benchmark reports contains a date in ISO 8061 format,
-- example: 2013-07-01T17:55:13-07:00.
-- See an implementation of `LocalDateTimeString()` in
-- Google Benchmark library source code [1].
--
-- 1. https://github.com/google/benchmark/blob/65668db27365d29ca4890cb2102e81acb6585b43/src/timers.cc#L209
-- @return e.g. 2024-07-18T10:06:59+0000
local function iso_8061_timestamp()
    local dt = os.date('*t')
    return datetime.new(dt):format('%Y-%m-%dT%H:%M:%S%z')
end

local function perf_context()
    return {
        build_flags = tarantool.build.flags,
        build_target = tarantool.build.target,
        date = iso_8061_timestamp(),
        host_name = io.popen('hostname'):read(),
        load_avg = load_average(),
        tarantool_version = tarantool.version,
    }
end

function M.new(opts)
    assert(type(opts) == 'table', 'given argument should be a table')
    local output_format = opts.output_format or 'console'
    return setmetatable({
        output = opts.output,
        output_format = output_format:lower(),
        results = {},
        context = perf_context(),
    }, {__index = {
        add_result = add_result,
        dump_results = dump_results,
    }})
end

return M
