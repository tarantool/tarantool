--
-- The test measures run time of various space select operations.
--
-- Output format (console):
-- <test-case> <run-time-nanoseconds>
--
-- Options:
-- --pattern <string>       run only tests matching the pattern;
--                          it's possible to specify more than one
--                          pattern separated by '|', for example,
--                          'get|select'
-- --read_view              use a read view
--
-- --output <string>        file to output
-- --output_format <string> console (default)|json

local clock = require('clock')
local fiber = require('fiber')

local params = require('internal.argparse').parse(arg, {
    {'output', 'string'},
    {'output_format', 'string'},
    {'pattern', 'string'},
    {'read_view', 'boolean'},
})
if params.pattern then
    params.pattern = string.split(params.pattern, '|')
end

local benchmark = require('benchmark').init(params)

box.cfg({log_level = 'error'})
box.once('perf_select_init', function()
    local s = box.schema.space.create('perf_select_space')
    s:create_index('primary')
    for i = 1, 1e3 do
        s:insert({i, 'data' .. i})
    end
    box.snapshot()
end)

local rv
local space
if params.read_view then
    rv = box.read_view.open()
    space = rv.space.perf_select_space
else
    space = box.space.perf_select_space
end

--
-- Array of test cases.
--
-- A test case is represented by a table with the following mandatory fields:
--
-- * name: test case name
-- * func: test function
--
local TESTS = {
    {
        name = 'get_0',
        func = function()
            space:get({1e6})
        end,
    },
    {
        name = 'get_1',
        func = function()
            space:get({10})
        end,
    },
    {
        name = 'select_0',
        func = function()
            space:select({1e6}, {iterator = 'ge', limit = 1})
        end,
    },
    {
        name = 'select_1',
        func = function()
            space:select({10}, {iterator = 'ge', limit = 1})
        end,
    },
    {
        name = 'select_5',
        func = function()
            space:select({10}, {iterator = 'ge', limit = 5})
        end,
    },
    {
        name = 'select_10',
        func = function()
            space:select({10}, {iterator = 'ge', limit = 10})
        end,
    },
    {
        name = 'select_after_0',
        func = function()
            space:select({10}, {iterator = 'ge', after = {1e6}, limit = 1})
        end,
    },
    {
        name = 'select_after_1',
        func = function()
            space:select({10}, {iterator = 'ge', after = {10}, limit = 1})
        end,
    },
    {
        name = 'select_after_5',
        func = function()
            space:select({10}, {iterator = 'ge', after = {10}, limit = 5})
        end,
    },
    {
        name = 'select_after_10',
        func = function()
            space:select({10}, {iterator = 'ge', after = {10}, limit = 10})
        end,
    },
    {
        name = 'select_fetch_pos_0',
        func = function()
            space:select({1e6}, {iterator = 'ge', fetch_pos = true, limit = 1})
        end,
    },
    {
        name = 'select_fetch_pos_1',
        func = function()
            space:select({10}, {iterator = 'ge', fetch_pos = true, limit = 1})
        end,
    },
    {
        name = 'select_fetch_pos_5',
        func = function()
            space:select({10}, {iterator = 'ge', fetch_pos = true, limit = 5})
        end,
    },
    {
        name = 'select_fetch_pos_10',
        func = function()
            space:select({10}, {iterator = 'ge', fetch_pos = true, limit = 10})
        end,
    },
    {
        name = 'pairs_0',
        func = function()
            for _, _ in space:pairs({1e6}, {iterator = 'ge'}) do
            end
        end,
    },
    {
        name = 'pairs_1',
        func = function()
            -- luacheck: ignore
            for _, _ in space:pairs({10}, {iterator = 'ge'}) do
                break
            end
        end,
    },
    {
        name = 'pairs_5',
        func = function()
            local c = 0
            for _, _ in space:pairs({10}, {iterator = 'ge'}) do
                c = c + 1
                if c == 5 then
                    break
                end
            end
        end,
    },
    {
        name = 'pairs_10',
        func = function()
            local c = 0
            for _, _ in space:pairs({10}, {iterator = 'ge'}) do
                c = c + 1
                if c == 10 then
                    break
                end
            end
        end,
    },
    {
        name = 'pairs_after_0',
        func = function()
            for _, _ in space:pairs({10}, {iterator = 'ge', after = {1e6}}) do
            end
        end,
    },
    {
        name = 'pairs_after_1',
        func = function()
            -- luacheck: ignore
            for _, _ in space:pairs({10}, {iterator = 'ge', after = {10}}) do
                break
            end
        end,
    },
    {
        name = 'pairs_after_5',
        func = function()
            local c = 0
            for _, _ in space:pairs({10}, {iterator = 'ge', after = {10}}) do
                c = c + 1
                if c == 5 then
                    break
                end
            end
        end,
    },
    {
        name = 'pairs_after_10',
        func = function()
            local c = 0
            for _, _ in space:pairs({10}, {iterator = 'ge', after = {10}}) do
                c = c + 1
                if c == 10 then
                    break
                end
            end
        end,
    },
    {
        name = 'tuple_pos',
        func = function()
            space.index.primary:tuple_pos({10})
        end
    },
}

local function single_run(func)
    local real_time_start = clock.time()
    local cpu_time_start = clock.proc()
    func()
    local delta_real = clock.time() - real_time_start
    local delta_cpu = clock.proc() - cpu_time_start
    return delta_real, delta_cpu
end

--
-- Runs the given test case function in a loop.
-- Returns the average time it takes to run the function once.
--
local function bench(test)
    local warmup_runs = 1e2 / 4
    local test_runs = 1e2
    local iters_per_run = 1e4
    local func = test.func
    local run = function()
        for _ = 1, iters_per_run do
            func()
        end
    end
    local real_time = 0
    local cpu_time = 0
    for i = 1, warmup_runs + test_runs do
        for _ = 1, 5 do
            collectgarbage('collect')
            jit.flush()
            fiber.yield()
        end
        local delta_real, delta_cpu = single_run(run)
        if i > warmup_runs then
            real_time = real_time + delta_real
            cpu_time = cpu_time + delta_cpu
        end
    end
    local iterations = test_runs * iters_per_run
    benchmark:add_result(test.name, real_time, cpu_time, iterations)
end

for _, test in ipairs(TESTS) do
    local skip = false
    if params.pattern then
        skip = true
        for _, pattern in ipairs(params.pattern) do
            if string.match(test.name, pattern) then
                skip = false
                break
            end
        end
    end
    if not skip then
        bench(test)
    end
end

benchmark:dump_results()

os.exit(0)
