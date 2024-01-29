--
-- The test measures run time of space full scan.
--
-- Output format:
-- <test-case> <run-time-seconds>
--
-- Options:
-- --engine <string>         space engine to use for the test
-- --column_count <number>   number of columns in the test space
-- --row_count <number>      number of rows in the test space
-- --use_read_view           use a read view
-- --use_scanner_api         use the column scanner API
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

local params = require('internal.argparse').parse(arg, {
    {'engine', 'string'},
    {'column_count', 'number'},
    {'row_count', 'number'},
    {'use_read_view', 'boolean'},
    {'use_scanner_api', 'boolean'},
})

local DEFAULT_ENGINE = 'memtx'
local DEFAULT_COLUMN_COUNT = 100
local DEFAULT_ROW_COUNT = 1000 * 1000

params.engine = params.engine or DEFAULT_ENGINE
params.column_count = params.column_count or DEFAULT_COLUMN_COUNT
params.row_count = params.row_count or DEFAULT_ROW_COUNT
params.use_read_view = params.use_read_view or false
params.use_scanner_api = params.use_scanner_api or false

local BUILDDIR = fio.abspath(fio.pathjoin(os.getenv('BUILDDIR') or '.'))
local MODULEPATH = fio.pathjoin(BUILDDIR, 'perf', 'lua',
                                '?.' .. tarantool.build.mod_format)

package.cpath = MODULEPATH .. ';' .. package.cpath

local test_module = require('column_scan_module')
local test_funcs = {}

for _, func_name in ipairs({'sum'}) do
    local full_func_name
    if params.use_scanner_api then
        full_func_name = func_name .. '_scanner'
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
        if i % 1000 then
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
        name = 'sum,first',
        func = function()
            local result = test_funcs.sum(box.space.test.id, 0, 0)
            local row_count = ffi.cast('uint64_t', params.row_count)
            check_result(result, row_count * (row_count + 1) / 2)
        end,
    },
    {
        name = 'sum,last',
        func = function()
            local result = test_funcs.sum(box.space.test.id, 0,
                                           params.column_count - 1)
            local row_count = ffi.cast('uint64_t', params.row_count)
            check_result(result, row_count * (row_count + 1) / 2)
        end,
    },
}

test_module.init()
fiber.set_max_slice(9000)

for _, test in ipairs(TESTS) do
    log.info('Running test %s...', test.name)
    test.func() -- warmup
    print(string.format('%s %.3f', test.name, clock.bench(test.func)[1]))
end

os.exit(0)
