local fio = require('fio')
local popen = require('popen')
local clock = require('clock')
local benchmark = require('benchmark')

local HELP = [[
   engine <string, 'memtx'>      - select engine type
   index <string, 'TREE'>        - select index type
   nohint <boolean>              - to turn the index hints off
   sk_count <number, 0>          - the amount of secondary indexes
   row_count <number, 1000000>   - the amount of tuples in the snapshot
   wal_row_count <number, 0>     - the amount of inserted tuples in the WAL
   wal_replace_count <number, 0> - the amount of replaced tuples in the WAL
   column_count <number, 1>      - the amount of fields in a single tuple
   memtx_sort_threads            - the amount of threads used for MemTX key sort
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
    {'memtx_sort_threads', 'number'},
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

-- The index sort thread number.
params.memtx_sort_threads = params.memtx_sort_threads or nil

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

local BUILDDIR = fio.abspath(fio.pathjoin(os.getenv('BUILDDIR') or '.'))
local SCRIPTPATH = fio.pathjoin(BUILDDIR, 'perf', 'lua', '?.lua')
local script = string.format([[
    package.path = '%s' .. ';' .. package.path
    require('recovery_script').run(%s, %s, '%s', '%s', '%s',
                                   %s, %d, %d, %d, %d, %d)
]], SCRIPTPATH, params.memtx_sort_threads, params.memtx_sort_data, WORK_DIR,
    params.engine, params.index, params.nohint, params.sk_count,
    params.column_count, params.row_count, params.wal_row_count,
    params.wal_replace_count)

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
