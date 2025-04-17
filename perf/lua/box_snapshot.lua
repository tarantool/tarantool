local log = require('log')
local fio = require('fio')
local clock = require('clock')
local benchmark = require('benchmark')

local HELP = [[
   engine <string, 'memtx'>     - select engine type
   index <string, 'TREE'>       - select index type
   nohint <boolean>             - to turn the index hints off
   sk_count <number, 0>         - the amount of secondary indexes
   row_count <number, 10000000> - the amount of tuples in the snapshot
   column_count <number, 1>     - the amount of fields in a single tuple
   memtx_sort_data <boolean>    - enable the MemTX sort data
   checkpoint_count <number, 1> - the amount of checkpoints to create
   help (same as -h)            - print this message
]]

local parsed_params = {
    {'engine', 'string'},
    {'index', 'string'},
    {'nohint', 'boolean'},
    {'sk_count', 'number'},
    {'row_count', 'number'},
    {'column_count', 'number'},
    {'memtx_sort_data', 'boolean'},
    {'checkpoint_count', 'number'},
}

local params = benchmark.argparse(arg, parsed_params, HELP)
local bench = benchmark.new(params)

-- Engine to use.
params.engine = params.engine or 'memtx'
assert(params.engine == 'memtx' or
       params.engine == 'vinyl' or
       params.engine == 'memcs')

-- Type of index to use.
params.index = string.upper(params.index or 'TREE')
assert(params.index == 'TREE' or params.index == 'HASH')

-- Whether to turn the index hints off or not.
params.nohint = params.nohint or false

-- Number of secondary indexes.
params.sk_count = params.sk_count or 0

-- Number of tuples in the snapshot.
params.row_count = params.row_count or 1000000

-- Number of fields in each tuple.
params.column_count = params.column_count or 1
assert(params.column_count >= 1)

-- Number of checkpoints to create.
params.checkpoint_count = params.checkpoint_count or 1
assert(params.checkpoint_count >= 1)

-- Whether to enable the MemTX sort data or not.
params.memtx_sort_data = params.memtx_sort_data or false

local WORK_DIR = string.format('box_snapshot,engine=%s,index=%s,nohint=%s,' ..
                               'sk_count=%d,row_count=%d,column_count=%d',
                               params.engine, params.index, params.nohint,
                               params.sk_count, params.row_count,
                               params.column_count)
fio.mkdir(WORK_DIR)

box.cfg{
    memtx_sort_data_enabled = params.memtx_sort_data,
    memtx_memory = 4*1024*1024*1024,
    work_dir = WORK_DIR,
    wal_mode = 'none',
    log = 'tarantool.log',
    checkpoint_count = 1,
}

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
    s:create_index('pk', {type = params.index, hint = not params.nohint})
    for i = 1, params.sk_count do
        s:create_index('sk' .. i, {type = params.index,
                                   hint = not params.nohint})
    end
    log.info('Generating the test data set...')
    local tuple = {}
    local pct_complete = 0
    box.begin()
    for i = 1, params.row_count do
        tuple[1] = i
        for j = 2, params.column_count do
            tuple[j] = math.random(1, 1000000)
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
    log.info('Done. Please restart the test.')
    os.exit()
end)

-- Warm-up.
box.space._space:alter({}) -- no-op to update vclock
box.snapshot()

-- Do the benchmark.
local real_time_start = clock.time()
local cpu_time_start = clock.proc()
for i = 1, params.checkpoint_count do  -- luacheck: no unused
    box.space._space:alter({}) -- no-op to update vclock
    box.snapshot()
end
local delta_real = clock.time() - real_time_start
local delta_cpu = clock.proc() - cpu_time_start
bench:add_result('box_snapshot', {
    real_time = delta_real,
    cpu_time = delta_cpu,
    items = params.checkpoint_count * params.row_count,
})

bench:dump_results()
os.exit(0)
