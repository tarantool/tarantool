#!/usr/bin/env tarantool

-- An instance file for the node which tests applier thread ack speed.
-- There are 10 threads, one per each replication source, so each WAL write
-- results in an ack message for each thread. This magnifies the possible
-- performance drawbacks of copying vclocks for each thread.

local mode = arg[1] or 'none'
assert(mode == 'write' or mode == 'none',
       "mode should be either 'write' or 'none'")

local id = tonumber(arg[2]) or 1

assert(id < 2 or id > 11,
       'The id should be outside of the occupied range [2, 11]')

local fiber = require('fiber')

box.cfg{
    listen = 3300 + id,
    replication = {
        3302,
        3303,
        3304,
        3305,
        3306,
        3307,
        3308,
        3309,
        3310,
        3311,
    },
    replication_threads = 10,
    -- Disable WAL on a node to notice slightest differences in TX thread
    -- performance. It's okay to replicate TO a node with disabled WAL. You only
    -- can't replicate FROM it.
    wal_mode = mode,
    work_dir = tostring(id),
    log = id..'.log',
}

box.schema.space.create('test', {if_not_exists = true})
box.space.test:create_index('pk', {if_not_exists = true})
box.snapshot()

local function replace_func(num_iters)
    for i = 1, num_iters do
        box.space.test:replace{i, i}
    end
end

local function test(num_fibers)
    local fibers = {}
    local num_replaces = 1e6
    local num_iters = num_replaces / num_fibers
    local start = fiber.time()
    for _ = 1, num_fibers do
        local fib = fiber.new(replace_func, num_iters)
        fib:set_joinable(true)
        table.insert(fibers, fib)
    end
    assert(#fibers == num_fibers, "Fibers created successfully")
    for _, fib in pairs(fibers) do
        fib:join()
    end
    -- Update fiber.time() if there were no yields.
    fiber.yield()
    local dt = fiber.time() - start
    return dt, num_replaces / dt
end

local mean_time = 0
local mean_rps = 0
local num_iters = 100

-- Fiber count > 1 makes no sense for wal_mode = 'none'. There are no yields
-- on replace when there are no wal writes.
local num_fibers = mode == 'none' and 1 or 100

for test_iter = 1,num_iters do
    local time, rps = test(num_fibers)
    print(('Iteration #%d finished in %f seconds. RPS: %f'):format(test_iter,
                                                                   time, rps))
    mean_time = mean_time + time / num_iters
    mean_rps = mean_rps + rps / num_iters
end
print(('Mean iteraion time: %f, mean RPS: %f'):format(mean_time, mean_rps))
os.exit()
