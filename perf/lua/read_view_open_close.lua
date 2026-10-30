local fio = require('fio')
local benchmark = require('benchmark')
local clock = require('clock')

local USAGE = [[
   field_count <number, 10000>  - the number of fields in the space format
   minimum_run_time <number, 5> - minimal time to run in seconds
]]

local params = benchmark.argparse(arg, {
    {'field_count', 'number'},
    {'minimum_run_time', 'number'},
}, USAGE)
local bench = benchmark.new(params)

params.field_count = params.field_count or 10000
params.minimum_run_time = params.minimum_run_time or 5

local test_dir = fio.tempdir()

box.cfg{
    log = 'tarantool.log',
    work_dir = test_dir,
}

local format = {}
for i = 1, params.field_count do
    table.insert(format, {name = 'field'..i, type = 'uint64'})
end

box.schema.space.create('test', {
    engine = 'memtx',
    format = format,
    field_count = params.field_count,
})

local start_time = {
    time = clock.time(),
    proc = clock.proc(),
}

local delta_real
local count = 0
repeat
    local rv = box.read_view.open()
    rv:close()
    delta_real = clock.time() - start_time.time
    count = count + 1
until delta_real > params.minimum_run_time

bench:add_result('open_close', {
    items = count,
    real_time = clock.time() - start_time.time,
    cpu_time = clock.proc() - start_time.proc,
})

bench:dump_results()

fio.rmtree(test_dir)
os.exit(0)
