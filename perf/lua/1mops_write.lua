#!/bin/env tarantool

local err, popen = pcall(require,'popen')
if err ~= true then
    print(err)
    popen = {}
end
local clock = require('clock')
local fiber = require('fiber')
local math = require('math')
local json = require('json')
local fio = require('fio')

-- XXX: This benchmark should be able to work standalone without
-- any provided libraries or set LUA_PATH. Add stubs for that
-- case.
local benchmark_is_available, benchmark = pcall(require, 'benchmark')

local USAGELINE = [[

 Usage: tarantool 1mops_write.lua [options]

]]

local HELP = [[
   engine <string, 'memtx'>   - select engine type
   fibers <number, 50>        - number of fibers to run simultaneously
   help (same as -h)          - print this message
   index <string, 'HASH'>     - select index type
   nodes <number, 1>          - number of nodes to test one-way replication
   nohint <boolean>           - to turn the index hints off
   ops <number, 10000000>     - total amount of replaces to be performed
   sync <boolean>             - to turn the synchro replication on
   transaction <number, 150>  - number of replaces in one transaction
   wal_mode <string, 'write'> - WAL synchronization mode
   warmup <number, 10>        - percent of ops to skip before measurement

 Being run without options, this benchmark tests the HASH index of the memtx
 engine by running 50 fibers that replace 150 integers, using each as a primary
 key. Refer to the 'fiber_load' function in sources to change the workload.
]]

local parsed_params = {
    {'engine', 'string'},
    {'fibers', 'number'},
    {'index', 'string'},
    {'nodes', 'number'},
    {'nohint', 'boolean'},
    {'ops', 'number'},
    {'sync', 'boolean'},
    {'transaction', 'number'},
    {'wal_mode', 'string'},
    {'warmup', 'number'},
}

local params
local bench

local function dump_benchmark_results(res)
    print(('%s %d rps'):format(res.name, res.items_per_second))
end

if benchmark_is_available then
    params = benchmark.argparse(arg, parsed_params, HELP)
    bench = benchmark.new(params)
else
    table.insert(parsed_params, {'h', 'boolean'})
    table.insert(parsed_params, {'help', 'boolean'})
    params = require('internal.argparse').parse(arg, parsed_params)
    if params.h or params.help then
        print(USAGELINE .. HELP)
        os.exit(0)
    end
    bench = {}
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

local function exit(res, details)
    if (details ~= nil) then
        print(details)
    end
    if test_dir ~= nil then
        rmtree(test_dir)
        test_dir = nil
    end
    os.exit(res)
end

-- turn true to test the synchronous replication
local test_sync = params.sync or false

-- number of operations performed by test
local num_ops = params.ops or 10000000

-- number of fibers
local num_fibers = params.fibers or 50

-- number of operations per transaction
local ops_per_txn = params.transaction or 150

-- number of nodes - master and replicas
local nodes = params.nodes or 1

-- engine to use
local engine = params.engine or 'memtx'
assert(engine == 'memtx' or engine == 'vinyl')

-- WAL mode
local wal_mode = params.wal_mode or 'write'
assert(wal_mode == 'write'
       or wal_mode == 'none'
       or wal_mode == 'fsync')

-- type of index to use
local index_config = {}
index_config.type = params.index or 'HASH'
index_config.type = string.upper(index_config.type)

local hints_msg = ''
if params.index == 'TREE' then
    hints_msg = ' with hints'
    if params.nohint then
        index_config.hint = false
        hints_msg = ' without hints'
    end
end

-- warm up part, not counted in average
local warmup_thr = params.warmup or 10
warmup_thr = warmup_thr > 100 and 100 or warmup_thr
warmup_thr = warmup_thr < 0 and 0 or warmup_thr
-- END OF TUNABLE OPTIONS

-- transactions per fiber
local trans_per_fiber = num_ops/ops_per_txn/num_fibers

-- by default no output from replicas are received
-- redirect it into master's one breaks terminal
local std_redirect = {
    stdin='devnull',
    stdout='devnull',
    stderr='devnull'
}

print(string.format([[
# making %d REPLACE operations,
# %d operations per txn,
# using %d fibers,
# in a replicaset of %d nodes,
# using %s index type%s
# with WAL mode %s
# ]],
      num_ops, ops_per_txn, num_fibers, nodes,
      index_config.type, hints_msg, wal_mode))

box.cfg{
    log_level = 0,
    listen = 0,
    read_only = false,
    memtx_memory = 2*1024*1024*1024,
    work_dir = test_dir,
    wal_mode = wal_mode,
}

box.schema.user.create('replicator', {password = 'password'})
box.schema.user.grant('replicator', 'replication')

-- number of nodes, storage for popen handles
local nodes_ph = {}

-- run replicas (if needed)
if (nodes > 1) then
    if (nodes ~= nil and nodes < 32 and nodes > 0) then
        if (type(popen) ~= 'table' or popen.new == nil) then
            exit(1, "can't run replication w/o popen")
        end
        print('# starting ' .. nodes - 1 .. ' replicas')
        for i = 3302, 3300+nodes do
            -- subdir for replica's data
            os.execute('mkdir ' .. i)
            -- command line for replica to start
            local cmd = {
                arg[-1],
                '-e',
                string.format([[
                box.cfg {
                    read_only = false,
                    log_level = 5,
                    replication = {'replicator:password@%s'},
                    work_dir = '%s',
                    replication_connect_quorum=1
                }]], box.info.listen, i)
            }
            local res, err = popen.new(cmd, std_redirect)
            if (res) then
                nodes_ph[i] = res
            else
                exit(1, 'error running replica: ' .. err)
            end
        end

        -- wait for all replicas to connect
        while #box.info.replication < nodes do
            print('# replication', #box.info.replication)
            fiber.sleep(0.1)
        end
    else
        exit(1, 'Incorrect number of nodes \"' .. arg[1] .. '\" must be 1..31')
    end
end

local space
local done = false
local err

if (test_sync) then
    box.cfg{replication_synchro_quorum = nodes}
    print('# promoting')
    box.ctl.promote()
    print('# done')
    space, err = box.schema.create_space('test',
                                         {engine = engine, is_sync = true})
else
    space, err = box.schema.create_space('test',
                                         {engine = engine})
end
if space == false then
    exit(1, 'error creating space ' .. json.encode(err))
end
local res, err = pcall(space.create_index, space, 'pk', index_config)
if (res ~= true) then
    exit(1, 'error creating index ' ..
         json.encode(index_config) .. ' :' .. json.encode(err))
end

-- THE load fiber
local function fiber_load(start, s)
    start = start % 1000000 -- limit the size of space to 1M elements
    for _ = 1, trans_per_fiber do
        box.begin()
        for _ = 1, ops_per_txn do
            s:replace{start}
            start = start + 1
        end
        box.commit()
        fiber.yield()
    end
end

-- fiber storage to join them
local fibers_storage = {}

collectgarbage()
collectgarbage()

-- start timer
local timer_begin = {
    clock.time(),
    clock.proc()
}

local max_rps = 0
local ops_done = 0

-- start a fiber to perform the peak RPS measurement
-- skip the warmup if any and restart the timer
fiber.create(function()
    fiber.create(function()
        while true do
            local prev_t = clock.time()
            local prev_lsn = box.info.lsn
            fiber.sleep(0.1)
            local rps = (box.info.lsn - prev_lsn) / (clock.time() - prev_t)
            if (rps > max_rps) then
                max_rps=rps
            end
        end
    end)
    if warmup_thr > 0 then
        io.write('# Warmup... ')
        io.flush()
        while box.info.lsn < num_ops / 100 * warmup_thr do
            fiber.sleep(0.001)
        end
        ops_done = box.info.lsn
        print('done, lsn: ', ops_done)
        timer_begin = {
            clock.time(),
            clock.proc()
        }
    end
end)

-- start fibers for the main load
for i = 1, num_fibers do
    fibers_storage[i] = fiber.create(fiber_load, i*num_ops, space)
    if (fibers_storage[i]:status() ~= 'dead') then
        fibers_storage[i]:wakeup() -- needed for backward compatibility with 1.7
    end
end

-- wait for all fibers to finish
for i = 1, num_fibers do
    while fibers_storage[i]:status() ~= 'dead' do
        fiber.sleep(0.001)
    end -- the loop is needed for backward compatibility with 1.7
end

ops_done = box.info.lsn - ops_done

-- stop timer for master
local res
local real_time = clock.time() - timer_begin[1]
local cpu_time = clock.proc() - timer_begin[2]
if benchmark_is_available then
    res = bench:add_result('1mops_master', {
        real_time = real_time,
        cpu_time = cpu_time,
        items = ops_done,
    })
else
    res = {
        name = '1mops_master',
        real_time = real_time,
        cpu_time = cpu_time,
        items = ops_done,
    }
    res.items_per_second = ops_done / res.real_time
end

print(string.format('# master done %d ops in time: %f, cpu: %f',
                    ops_done, res.real_time, res.cpu_time))
print('# master average speed', res.items_per_second, 'ops/sec')
print('# master peak speed', math.floor(max_rps), 'ops/sec')
-- Dump results early if there is no module for it.
if not benchmark_is_available then
    dump_benchmark_results(res)
end

-- wait for all replicas and kill them
if nodes > 1 then
    while(not done) do
        for i = 2, nodes do
            local r_vclock = box.info.replication[i].downstream.vclock
            if (r_vclock and
                r_vclock[1] < ops_done) then
                done = false
                break
            end
            done = true
        end
        if (not done) then
            fiber.sleep(0.001)
        end
    end
    -- stop timer for replicas
    local real_time_replica = clock.time() - timer_begin[1]
    local cpu_time_replica = clock.proc() - timer_begin[2]
    if benchmark_is_available then
        res = bench:add_result('1mops_replica', {
            real_time = real_time_replica,
            cpu_time = cpu_time_replica,
            items = ops_done,
        })
    else
        res = {
            name = '1mops_replica',
            real_time = real_time_replica,
            cpu_time = cpu_time_replica,
            items = ops_done,
        }
        res.items_per_second = ops_done / res.real_time
    end


    print(string.format('# replicas done %d ops in time: %f, cpu: %f',
                        ops_done, res.real_time, res.cpu_time))
    -- Dump results early if there is no module for it.
    if not benchmark_is_available then
        dump_benchmark_results(res)
    end

    for _, replica in pairs(nodes_ph) do
        replica:kill()
        replica:wait()
    end
end

if benchmark_is_available then
    bench:dump_results()
end

exit(0)
