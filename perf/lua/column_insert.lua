--
-- The test measures run time of batch insertion into the space columns.
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
   engine <string, 'memtx'>          - space engine to use for the test
   wal_mode <string, 'write'>        - write-ahead log mode to use for the test
   sparse_mode <string, 'rand'>      -
     * seq - the first 10% (column_count_batch / column_count_total) columns are
             filled in sequential order;
     * rand - sparse columns are randomly distributed:
              in non-Arrow mode columns are randomly chosen for every row;
              in Arrow mode columns are randomly chosen for every batch.
   column_count_total <number, 1000> - number of columns in the test space
   column_count_batch <number, 100>  - number of columns in the record batch
   row_count_total <number, 1000000> - number of inserted rows
   row_count_batch <number, 1000>    - number of rows per record batch
   use_arrow_api <boolean, false>    - use the Arrow API for batch insertion

]]

local params = benchmark.argparse(arg, {
    {'engine', 'string'},
    {'wal_mode', 'string'},
    {'sparse_mode', 'string'},
    {'column_count_total', 'number'},
    {'column_count_batch', 'number'},
    {'row_count_total', 'number'},
    {'row_count_batch', 'number'},
    {'use_arrow_api', 'boolean'},
}, USAGE)

local DEFAULT_ENGINE = 'memtx'
local DEFAULT_WAL_MODE = 'write'
local DEFAULT_SPARSE_MODE = 'rand'
local DEFAULT_COLUMN_COUNT_TOTAL = 1000
local DEFAULT_COLUMN_COUNT_BATCH = 100
local DEFAULT_ROW_COUNT_TOTAL = 1000 * 1000
local DEFAULT_ROW_COUNT_BATCH = 1000

params.engine = params.engine or DEFAULT_ENGINE
params.wal_mode = params.wal_mode or DEFAULT_WAL_MODE
params.sparse_mode = params.sparse_mode or DEFAULT_SPARSE_MODE
params.column_count_total = params.column_count_total or
                            DEFAULT_COLUMN_COUNT_TOTAL
params.column_count_batch = params.column_count_batch or
                            DEFAULT_COLUMN_COUNT_BATCH
params.row_count_total = params.row_count_total or DEFAULT_ROW_COUNT_TOTAL
params.row_count_batch = params.row_count_batch or DEFAULT_ROW_COUNT_BATCH
params.use_arrow_api = params.use_arrow_api or false

assert(params.column_count_batch <= params.column_count_total)
assert(params.column_count_batch < 1000 * 1000)
assert(params.row_count_batch <= params.row_count_total)
assert(params.row_count_total % params.row_count_batch == 0)

local bench = benchmark.new(params)

local BUILDDIR = fio.abspath(fio.pathjoin(os.getenv('BUILDDIR') or '.'))
local MODULEPATH = fio.pathjoin(BUILDDIR, 'perf', 'lua',
                                '?.' .. tarantool.build.mod_format)
package.cpath = MODULEPATH .. ';' .. package.cpath

local test_module_name = 'column_insert_module'
local has_test_module, test_module = pcall(require, test_module_name)
if not has_test_module then
    local errmsg = ('Lua module "%s" is not found.\n'):format(test_module_name)
    io.stderr:write(errmsg)
    os.exit(1)
end

local test_funcs = {}
for _, func_name in ipairs({'insert'}) do
    local full_func_name
    if params.use_arrow_api then
        full_func_name = func_name .. '_batch'
    else
        full_func_name = func_name .. '_serial'
    end
    local f = test_module[full_func_name]
    if f == nil then
        error('The specified test mode is not supported by this build')
    end
    test_funcs[func_name] = f
end

local test_dir = fio.tempdir()

local function rmtree(s)
    if (fio.path.is_file(s) or fio.path.is_link(s)) then
        fio.unlink(s)
        return
    end
    if fio.path.is_dir(s) then
        for _,i in pairs(fio.listdir(s)) do
            rmtree(s..'/'..i)
        end
        fio.rmdir(s)
    end
end

box.cfg({
    log = 'tarantool.log',
    work_dir = test_dir,
    wal_mode = params.wal_mode,
    memtx_memory = 4 * 1024 * 1024 * 1024,
    checkpoint_count = 1,
})

box.once('init', function()
    log.info('Creating the test space...')
    local format = {}
    for i = 1, params.column_count_total do
        table.insert(format, {'field_' .. i, 'uint64', is_nullable = true})
    end
    format[1].is_nullable = false
    local s = box.schema.space.create('test', {
        engine = params.engine,
        field_count = #format,
        format = format,
    })
    s:create_index('pk')
end)

local function check_result(result, expected)
    log.info('expected %s, got %s', expected, result)
    assert(result == expected)
end

local TESTS = {
    {
        name = 'insert',
        func = function()
            test_funcs.insert(box.space.test.id, params)
            check_result(box.space.test:count(), params.row_count_total)
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
        items = params.row_count_total,
    })
end

fiber.set_max_slice(9000)
test_module.init(params)

for _, test in ipairs(TESTS) do
    log.info('Running test %s...', test.name)
    run_test(test)
end

bench:dump_results()

test_module.fini()
rmtree(test_dir)
os.exit(0)
