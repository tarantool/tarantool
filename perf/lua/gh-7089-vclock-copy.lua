local fiber = require('fiber')
local fio = require('fio')
local popen = require('popen')
local clock = require('clock')

local benchmark = require('benchmark')

local USAGE = [[
   nodes <number, 10>        - number of nodes as replication sources
   wal_mode <string, 'none'> - WAL mode for tested replica ('none', 'write')

 Being run without options, this benchmark tests applier thread ACK speed.
 There are 10 threads, one per replication source, so each WAL write results in
 an ACK message for each thread. This magnifies the possible performance
 drawbacks of copying vclocks for each thread. The test performs 1000000
 replaces, which are repeated 40 times, and measures the average RPS.
]]

local params = benchmark.argparse(arg, {
    {'nodes', 'number'},
    {'wal_mode', 'string'},
}, USAGE)

local bench = benchmark.new(params)

local wal_mode = params.wal_mode or 'none'
assert(wal_mode == 'write' or wal_mode == 'none',
       "mode should be either 'write' or 'none'")

-- Number of nodes.
local nodes = params.nodes or 10
assert(nodes > 0 and nodes < 32, 'incorrect nodes number')

local test_dir = fio.tempdir()

local function rmtree(s)
    if (fio.path.is_file(s) or fio.path.is_link(s)) then
        fio.unlink(s)
        return
    end
    if fio.path.is_dir(s) then
        for _, file in pairs(fio.listdir(s)) do
            rmtree(s .. '/' .. file)
        end
        fio.rmdir(s)
    end
end

-- Number of nodes, storage for popen handles.
local nodes_ph = {}

local function exit(res, details)
    for listen, master in pairs(nodes_ph) do
        print(('# killing node on %d'):format(listen))
        master:kill()
        master:wait()
    end

    if (details ~= nil) then
        print(details)
    end
    if test_dir ~= nil then
        rmtree(test_dir)
        test_dir = nil
    end
    os.exit(res)
end

-- The port for replica.
local LISTEN_PORT = 3301

local master_nodes = {}
for i = 3302, 3301 + nodes do
  table.insert(master_nodes, ('%d'):format(i))
end

local function bootstrap_node(listen)
    local work_dir = ('%s/%d'):format(test_dir, listen)
    -- Subdirectory for node's data.
    os.execute('mkdir ' .. work_dir)

    local cmd = {arg[-1], '-e', string.format([[
        local fiber = require('fiber')
        box.cfg {
            listen = %d,
            work_dir = '%s',
            read_only = false,
            replication = {%s},
            log = 'log.log',
        }
        box.once('bootstrap', function()
            box.schema.user.grant('guest', 'replication')
        end)

        repeat
            fiber.sleep(0.1)
        until not (#box.info.replication < %d or box.info().status ~= 'running')

        -- This is executed on every instance so that vclock is
        -- non-empty in each component. This will make the testing
        -- instance copy a larger portion of data on each write
        -- and make the performance degradation, if any.
        box.space._schema:replace({'Something to bump vclock ' .. %d})
    ]], listen, work_dir, table.concat(master_nodes, ','), nodes, listen)}
    local res, err = popen.new(cmd)

    if not res then
        exit(1, 'error running replica: ' .. err)
    end

    nodes_ph[listen] = res
end


if (nodes ~= nil and nodes < 32 and nodes > 0) then
    print('# starting ' .. nodes .. ' masters')
    for listen = 3302, 3301 + nodes do
        bootstrap_node(listen)
    end
else
    exit(1, 'Incorrect number of nodes: "' .. arg[1] .. '" must be 1..31')
end

box.cfg{
    listen = LISTEN_PORT,
    replication_threads = nodes,
    -- Disable WAL on a node to notice slightest differences in TX
    -- thread performance. It's okay to replicate _to_ a node with
    -- disabled WAL. You only can't replicate _from_ it.
    wal_mode = wal_mode,
    replication = master_nodes,
    read_only = false,
    log = 'test.log',
    work_dir = test_dir,
}

-- Wait for all nodes to connect.
repeat
    fiber.sleep(0.1)
    print('# replication', #box.info.replication,
          'status ', box.info().status)
until not (#box.info.replication < nodes or box.info().status ~= 'running')

box.schema.space.create('test', {if_not_exists = true})
box.space.test:create_index('pk', {if_not_exists = true})
box.snapshot()

local function replace_func(num_iters)
    for i = 1, num_iters do
        box.space.test:replace({i, i})
    end
end

local num_replaces = 1e6

local function test(num_fibers)
    local fibers = {}
    local num_iters = num_replaces / num_fibers
    local start_realtime = clock.time()
    local start_cputime = clock.proc()
    for _ = 1, num_fibers do
        local fib = fiber.new(replace_func, num_iters)
        fib:set_joinable(true)
        table.insert(fibers, fib)
    end
    assert(#fibers == num_fibers, 'Fibers created successfully')
    for _, fib in pairs(fibers) do
        fib:join()
    end
    local dt_realtime = clock.time() - start_realtime
    local dt_cputime = clock.proc() - start_cputime
    return dt_realtime, dt_cputime, num_replaces / dt_realtime
end

local num_iters = 40

-- Fiber count > 1 makes no sense for `wal_mode = 'none'`. There
-- are no yields on replace when there are no WAL writes.
local num_fibers = wal_mode == 'none' and 1 or 100

local total_realtime = 0
local total_cputime = 0
for test_iter = 1, num_iters do
    local realtime, cputime, rps = test(num_fibers)
    print(('# Iteration #%d finished in %f seconds. RPS: %f'):format(
        test_iter, realtime, rps
    ))
    total_realtime = total_realtime + realtime
    total_cputime = total_cputime + cputime
end

bench:add_result('walmode_' .. wal_mode, {
    real_time = total_realtime,
    cpu_time =  total_cputime,
    items = num_iters * num_replaces,
})

bench:dump_results()

exit(0)
