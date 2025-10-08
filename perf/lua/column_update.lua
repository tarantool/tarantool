--
-- The test measures run time of random column updates.
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
local log = require('log')
local tarantool = require('tarantool')
local benchmark = require('benchmark')

local USAGE = [[
   update_count <number, 1000000> - execute UPDATE this amount of times
   column_count <number, 2>       - number of columns in the test space
   row_count <number, 1000000>    - number of rows in the test space
   engine <string, 'memtx'>       - space engine to use for the test
]]

local params = benchmark.argparse(arg, {
    {'update_count', 'number'},
    {'column_count', 'number'},
    {'row_count', 'number'},
    {'engine', 'string'},
}, USAGE)

local DEFAULT_UPDATE_COUNT = 1000000
local DEFAULT_COLUMN_COUNT = 2
local DEFAULT_ROW_COUNT = 1000000
local DEFAULT_ENGINE = 'memtx'

params.update_count = params.update_count or DEFAULT_UPDATE_COUNT
params.column_count = params.column_count or DEFAULT_COLUMN_COUNT
params.row_count = params.row_count or DEFAULT_ROW_COUNT
params.engine = params.engine or DEFAULT_ENGINE

if params.row_count <= 1 then
    io.stderr:write('The column count must be at least 2\n')
    os.exit(1)
end

local bench = benchmark.new(params)

local BUILDDIR = fio.abspath(fio.pathjoin(os.getenv('BUILDDIR') or '.'))
local MODULEPATH = fio.pathjoin(BUILDDIR, 'perf', 'lua',
                                '?.' .. tarantool.build.mod_format)

package.cpath = MODULEPATH .. ';' .. package.cpath

local has_column_update, test_module = pcall(require, 'column_update_module')
if not has_column_update then
    io.stderr:write('Lua module "column_update_module" is not found.\n')
    os.exit(1)
end

local WORK_DIR = string.format(
    'column_update,engine=%s,column_count=%d,row_count=%d',
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

local TESTS = {
    {
        name = 'C',
        func = function()
            test_module.test(box.space.test.id, 0,
                             params.update_count,
                             params.column_count)
        end,
    },
    {
        name = 'lua',
        func = function()
            for i = 1, params.update_count do
                -- Do not update the first field (primary key).
                local fieldno = 1 + math.random(params.column_count - 1)
                box.space.test:update({i}, {{'=', fieldno, 0}})
            end
        end,
    },
}

local function run_test(test)
    local real_time_start = clock.time()
    local cpu_time_start = clock.proc()
    test.func()
    local delta_real = clock.time() - real_time_start
    local delta_cpu = clock.proc() - cpu_time_start
    bench:add_result(test.name, {
        real_time = delta_real,
        cpu_time = delta_cpu,
        items = params.update_count,
    })
end

fiber.set_max_slice(9000)

for _, test in ipairs(TESTS) do
    log.info('Running test %s...', test.name)
    test.func() -- warmup
    collectgarbage()
    run_test(test)
    collectgarbage()
end

bench:dump_results()

os.exit(0)
