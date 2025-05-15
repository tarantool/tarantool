--
-- The test measures run time of space full scan.
--
-- Output format (console):
-- <test-case> <rows-per-second>
--
-- NOTE: The test requires a C module. Set the BUILDDIR environment variable to
-- the tarantool build directory if using out-of-source build.
--

local clock = require('clock')
local fiber = require('fiber')
local fio = require('fio')
local ffi = require('ffi')
local log = require('log')
local tarantool = require('tarantool')
local benchmark = require('benchmark')

local USAGE = [[
   column_count <number, 100>       - number of columns in the test space
   engine <string, 'memtx'>         - space engine to use for the test
   row_count <number, 1000000>      - number of rows in the test space
   use_read_view <boolean, false>   - use a read view
   use_arrow_api <boolean, false>   - use the arrow stream API

 Being run without options, this benchmark measures the run time of a full scan
 from the space.
]]

local params = benchmark.argparse(arg, {
    {'column_count', 'number'},
    {'engine', 'string'},
    {'row_count', 'number'},
    {'use_read_view', 'boolean'},
    {'use_arrow_api', 'boolean'},
}, USAGE)

local DEFAULT_ENGINE = 'memtx'
local DEFAULT_COLUMN_COUNT = 100
local DEFAULT_ROW_COUNT = 1000 * 1000

params.engine = params.engine or DEFAULT_ENGINE
params.column_count = params.column_count or DEFAULT_COLUMN_COUNT
params.row_count = params.row_count or DEFAULT_ROW_COUNT
params.use_read_view = params.use_read_view or false
params.use_arrow_api = params.use_arrow_api or false

local bench = benchmark.new(params)

local BUILDDIR = fio.abspath(fio.pathjoin(os.getenv('BUILDDIR') or '.'))
local MODULEPATH = fio.pathjoin(BUILDDIR, 'perf', 'lua',
                                '?.' .. tarantool.build.mod_format)

package.cpath = MODULEPATH .. ';' .. package.cpath

local has_column_scan, test_module = pcall(require, 'column_scan_module')
if not has_column_scan then
    io.stderr:write('Lua module "column_scan_module" is not found.\n')
    os.exit(1)
end
local test_funcs = {}

for _, func_name in ipairs({'sum'}) do
    local full_func_name
    if params.use_arrow_api then
        full_func_name = func_name .. '_arrow'
    else
        full_func_name = func_name .. '_iterator'
    end
    if params.use_read_view then
        full_func_name = full_func_name .. '_rv'
    end
    local f = test_module[full_func_name]
    if f == nil then
        error('The specified test mode is not supported by this build')
    end
    test_funcs[func_name] = f
end

local WORK_DIR = string.format(
    'column_scan,engine=%s,column_count=%d,row_count=%d',
    params.engine, params.column_count, params.row_count)

fio.mkdir(WORK_DIR)

box.cfg({
    work_dir = WORK_DIR,
    log = 'tarantool.log',
    memtx_memory = 4 * 1024 * 1024 * 1024,
    checkpoint_count = 1,
})

box.once('init', function()
    log.info('Creating the test space...')
    local format = {}
    for i = 1, params.column_count do
        table.insert(format, {'field_' .. i, 'unsigned'})
    end
    local s = box.schema.space.create('test', {
        engine = params.engine,
        field_count = #format,
        format = format,
    })
    s:create_index('pk')
    log.info('Generating the test data set...')
    local tuple = {}
    local pct_complete = 0
    box.begin()
    for i = 1, params.row_count do
        for j = 1, params.column_count do
            if j % 2 == 1 then
                tuple[j] = i
            else
                tuple[j] = params.row_count - i + 1
            end
        end
        s:insert(tuple)
        if i % 1000 == 0 then
            box.commit()
            local pct = math.floor(100 * i / params.row_count)
            if pct ~= pct_complete then
                log.info('%d%% complete', pct)
                pct_complete = pct
            end
            box.begin()
        end
    end
    box.commit()
    log.info('Writing a snapshot...')
    box.snapshot()
end)

local function check_result(result, expected)
    log.info('expected %s, got %s', expected, result)
    assert(result == expected)
end

local TESTS = {
    {
        name = 'sum_first',
        func = function()
            local result = test_funcs.sum(box.space.test.id, 0, 0)
            local row_count = ffi.cast('uint64_t', params.row_count)
            check_result(result, row_count * (row_count + 1) / 2)
        end,
    },
    {
        name = 'sum_last',
        func = function()
            local result = test_funcs.sum(box.space.test.id, 0,
                                           params.column_count - 1)
            local row_count = ffi.cast('uint64_t', params.row_count)
            check_result(result, row_count * (row_count + 1) / 2)
        end,
    },
}

local function run_test(test)
    local func = test.func
    local real_time_start = clock.time()
    local cpu_time_start = clock.proc()
    func()
    local delta_real = clock.time() - real_time_start
    local delta_cpu = clock.proc() - cpu_time_start
    bench:add_result(test.name, {
        real_time = delta_real,
        cpu_time = delta_cpu,
        items = params.row_count,
    })
end

fiber.set_max_slice(9000)

for _, test in ipairs(TESTS) do
    log.info('Running test %s...', test.name)
    test.func() -- warmup
    run_test(test)
end

bench:dump_results()

os.exit(0)
