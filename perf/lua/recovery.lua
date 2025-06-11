local fio = require('fio')
local popen = require('popen')
local clock = require('clock')
local benchmark = require('benchmark')

local HELP = [[
   engine <string, 'memtx'>      - select engine type
   index <string, 'TREE'>        - select index type
   nohint <boolean>              - to turn the index hints off
   sk_count <number, 0>          - the amount of secondary indexes
   row_count <number, 10000000>  - the amount of tuples in the snapshot
   wal_row_count <number, 0>     - the amount of inserted tuples in the WAL
   wal_replace_count <number, 0> - the amount of replaced tuples in the WAL
   column_count <number, 1>      - the amount of fields in a single tuple
   memtx_sort_data <boolean>     - enable the MemTX sort data
   recovery_count <number, 1>    - the amount of recoveries to perform
   help (same as -h)             - print this message
]]

local parsed_params = {
    {'engine', 'string'},
    {'index', 'string'},
    {'nohint', 'boolean'},
    {'sk_count', 'number'},
    {'row_count', 'number'},
    {'wal_row_count', 'number'},
    {'wal_replace_count', 'number'},
    {'column_count', 'number'},
    {'memtx_sort_data', 'boolean'},
    {'recovery_count', 'number'},
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

-- Number of inserted tuples in the WAL.
params.wal_row_count = params.wal_row_count or 0

-- Number of replaced tuples in the WAL.
params.wal_replace_count = params.wal_replace_count or 0
assert(params.wal_replace_count <= params.row_count + params.wal_row_count)

-- Number of fields in each tuple.
params.column_count = params.column_count or 1 + params.sk_count
assert(params.column_count >= 1 + params.sk_count)

-- Number of checkpoints to create.
params.recovery_count = params.recovery_count or 1
assert(params.recovery_count >= 1)

-- Whether to enable the MemTX sort data or not.
params.memtx_sort_data = params.memtx_sort_data or false

local WORK_DIR = string.format('recovery,engine=%s,index=%s,nohint=%s,' ..
                               'sk_count=%d,column_count=%d,row_count=%d,' ..
                               'wal_row_count=%d,wal_replace_count=%d',
                               params.engine, params.index, params.nohint,
                               params.sk_count, params.column_count,
                               params.row_count, params.wal_row_count,
                               params.wal_replace_count)
fio.mkdir(WORK_DIR)

local script = string.format([[
local log = require('log')
box.cfg{
    memtx_sort_data_enabled = %s,
    memtx_memory = 4*1024*1024*1024,
    work_dir = '%s',
    log = 'tarantool.log',
    checkpoint_count = 1,
}
box.once('init', function()
    local engine = %s
    local index = %s
    local nohint = %s
    local sk_count = %d
    local column_count = %d
    local row_count = %d
    local wal_row_count = %d
    local wal_replace_count = %d
    log.info('Creating the test space...')
    local format = {}
    for i = 1, column_count do
        table.insert(format, {'field_' .. i, 'unsigned'})
    end
    local s = box.schema.space.create('test', {
        engine = engine,
        field_count = #format,
        format = format,
    })
    s:create_index('pk', {type = index, hint = not nohint})
    for i = 1, sk_count do
        s:create_index('sk' .. i, {parts = {{i + 1, 'unsigned'}},
                                   type = index, hint = not nohint,
                                   unique = false})
    end
    local tuple = {}
    local function write_rows(n, base)
        local pct_complete = 0
        box.begin()
        for i = 1, n do
            tuple[1] = base + i
            for j = 2, column_count do
                tuple[j] = math.random(1, 1000000)
            end
            s:replace(tuple)
            if i %% 1000 == 0 then
                box.commit()
                local pct = math.floor(100 * i / n)
                if pct ~= pct_complete then
                    log.info('%%d%%%% complete', pct)
                    pct_complete = pct
                end
                box.begin()
            end
        end
        box.commit()
    end
    log.info('Generating the test data set...')
    write_rows(row_count, 0)
    log.info('Writing a snapshot...')
    box.snapshot()
    log.info('Writing WAL (inserts)...')
    write_rows(wal_row_count, row_count)
    log.info('Writing WAL (replaces)...')
    write_rows(wal_replace_count, 0)
    log.info('Done.')
end)
os.exit(0)
]], params.memtx_sort_data, WORK_DIR, params.engine, params.index,
    params.nohint, params.sk_count, params.column_count, params.row_count,
    params.wal_row_count, params.wal_replace_count)

local function run_tarantool()
    local cmd = {arg[-1], '-e', script}
    local std_redirect = {
        stdin = popen.opts.PIPE,
        stdout = popen.opts.PIPE,
        stderr = popen.opts.PIPE,
    }
    local ph, err = popen.new(cmd, std_redirect)
    if ph == nil then
        print(err)
        os.exit()
    end
    local res = ph:wait()
    assert(res.state == 'exited')
    assert(res.exit_code == 0)
end

-- Warm-up and create the snapshot if it does not exist.
run_tarantool()

-- Do the benchmark.
local real_time_start = clock.time()
local cpu_time_start = clock.proc()
for i = 1, params.recovery_count do -- luacheck: no unused
    run_tarantool()
end
local delta_real = clock.time() - real_time_start
local delta_cpu = clock.proc() - cpu_time_start
bench:add_result('recovery', {
    real_time = delta_real,
    cpu_time = delta_cpu,
    items = params.recovery_count * (params.row_count + params.wal_row_count),
})

bench:dump_results()
os.exit(0)
