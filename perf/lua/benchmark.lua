local json = require('json')

local M = {}

local function format_report(bench)
    local output_format = bench.output_format
    local results = bench.results
    local report = ''
    if output_format == 'json' then
        -- The output should have the same format as the Google
        -- Benchmark.
        report = json.encode({benchmarks = results})
    else
        assert(output_format == 'console', 'unkonwn output format')
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
        local fh = assert(io.open(output, 'w'))
        fh:write(text)
        fh:close()
    else
        io.stdout:write(text)
    end
end

local function add_result(bench, name, real_time, cpu_time, items)
    local items_per_second = math.floor(items / real_time)
    local result = {
        name = name,
        real_time = real_time,
        cpu_time = cpu_time,
        iterations = items,
        items_per_second = items_per_second,
    }
    table.insert(bench.results, result)
    return result
end

local function dump_results(bench)
    dump_report(bench, format_report(bench))
end

function M.init(opts)
    assert(type(opts) == 'table', 'given argument should be a table')
    return setmetatable({
        output = opts.output,
        output_format = opts.output_format or 'console',
        results = {},
    }, {__index = {
        add_result = add_result,
        dump_results = dump_results,
    }})
end

return M
