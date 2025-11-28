local fio = require('fio')
local benchmark = require('benchmark')
local clock = require('clock')

local USAGE = [[
   field_count <number, 10000>  - the number of fields in the space format
   iters <number, 10>           - the number of iterations (open + close
                                  read view)
]]

local params = benchmark.argparse(arg, {
    {'field_count', 'number'},
    {'iters', 'number'},
}, USAGE)
local bench = benchmark.new(params)

params.field_count = params.field_count or 10000
params.iters = params.iters or 10

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

for _ = 1, params.iters do
    local rv = box.read_view.open()
    rv:close()
end

bench:add_result('open_close', {
    items = params.iters,
    real_time = clock.time() - start_time.time,
    cpu_time = clock.proc() - start_time.proc,
})

bench:dump_results()

fio.rmtree(test_dir)
os.exit(0)
