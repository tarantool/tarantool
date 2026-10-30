--
-- The test measures run time of updates of space columns.
--
-- Output format (console):
-- <test-case> <rows-per-second>
--

local clock = require('clock')
local csv = require('csv')
local fiber = require('fiber')
local fio = require('fio')
local log = require('log')
local benchmark = require('benchmark')

local USAGE = [[
   engine <string, 'memtx'>          - space engine to use for the test
   wal_mode <string, 'write'>        - write-ahead log mode to use for the test
   column_count <number, 1000>       - number of columns in the test space
   row_count <number, 1000000>       - number of inserted rows
   secondary_indexes <number, 0>     - amount of secondary indexes,
                                       each is built over the second field
   update_fields <string, "2">       - comma-separated list of update fields
   update_count <number, 1000>       - amount of updates to perform
   minimum_run_time <number, 5>      - minimal time to run distinct perf test
                                       in seconds
]]

local params = benchmark.argparse(arg, {
    {'engine', 'string'},
    {'wal_mode', 'string'},
    {'column_count', 'number'},
    {'row_count', 'number'},
    {'secondary_indexes', 'number'},
    {'update_fields', 'string'},
    {'update_count', 'number'},
    {'minimum_run_time', 'number'},
}, USAGE)

local DEFAULT_ENGINE = 'memtx'
local DEFAULT_WAL_MODE = 'write'
local DEFAULT_COLUMN_COUNT = 1000
local DEFAULT_ROW_COUNT = 1000 * 1000
local DEFAULT_SECONDARY_INDEXES = 0
local DEFAULT_UPDATE_FIELDS = '2'
local DEFAULT_UPDATE_COUNT = 1000

params.engine = params.engine or DEFAULT_ENGINE
params.wal_mode = params.wal_mode or DEFAULT_WAL_MODE
params.column_count = params.column_count_total or
                      DEFAULT_COLUMN_COUNT
params.row_count = params.row_count or DEFAULT_ROW_COUNT
params.secondary_indexes = params.secondary_indexes or DEFAULT_SECONDARY_INDEXES
params.update_fields = params.update_fields or DEFAULT_UPDATE_FIELDS
params.update_count = params.update_count or DEFAULT_UPDATE_COUNT
params.minimum_run_time = params.minimum_run_time or 5

assert(params.secondary_indexes >= 0)
params.update_fields = csv.load(params.update_fields)[1]

local bench = benchmark.new(params)

local test_dir = fio.tempdir()

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
    for i = 1, params.column_count do
        table.insert(format, {'field_' .. i, 'uint64', is_nullable = true})
    end
    format[1].is_nullable = false
    local s = box.schema.space.create('test', {
        engine = params.engine,
        field_count = #format,
        format = format,
    })
    s:create_index('pk')
    for i = 1, params.secondary_indexes do
        s:create_index('sk' .. i, {parts = {{1}}, covers = {{2}}})
    end
    fiber.set_max_slice(9000)
    box.begin()
    for i = 1, params.row_count do
        if i % 1000 == 0 then
            box.commit()
            box.begin()
        end
        local tuple = {i}
        for _ = 2, params.column_count do
            table.insert(tuple, box.NULL)
        end
        box.space.test:insert(tuple)
    end
    box.commit()
end)

local function update_test(row_count, update_count, update_fields)
    local ops = {}
    for _, fieldno in ipairs(update_fields) do
       table.insert(ops, {'=', tonumber(fieldno), 42})
    end
    box.begin()
    for i = 1, update_count do
        if i % 1000 == 0 then
            box.commit()
            box.begin()
        end
        box.space.test:update(i % row_count + 1, ops)
    end
    box.commit()
end

local TESTS = {
    {
        name = 'update',
        func = function()
            update_test(params.row_count, params.update_count,
                        params.update_fields)
        end,
    },
}

local function run_test(test)
    local func = test.func
    func() -- warmup

    local real_time_start = clock.time()
    local cpu_time_start = clock.proc()
    local delta_real
    local count = 0
    repeat
        func()
        delta_real = clock.time() - real_time_start
        count = count + 1
    until delta_real > params.minimum_run_time
    local delta_cpu = clock.proc() - cpu_time_start
    bench:add_result(test.name, {
        real_time = delta_real,
        cpu_time = delta_cpu,
        items = params.row_count * count,
    })
end

fiber.set_max_slice(9000)

for _, test in ipairs(TESTS) do
    log.info('Running test %s...', test.name)
    run_test(test)
end

bench:dump_results()
os.exit(0)
