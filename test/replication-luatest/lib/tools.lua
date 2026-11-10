--
-- General helpers for the tests working with several instances.
--
local fiber = require('fiber')

local M = {}

local DEFAULT_TIMEOUT = 600

-- Run the jobs, functions without arguments, each in a fiber of its own, and
-- return when all of them are done. The jobs spending their time in waits run
-- in parallel this way. If some jobs fail, the error of the first of them in
-- the list is raised, after all the jobs are done.
--
-- The timeout bounds the whole run, 10 minutes by default. When it runs out,
-- the jobs still running are cancelled and an error is raised.
function M.run_in_parallel(jobs, timeout)
    timeout = timeout or DEFAULT_TIMEOUT
    local deadline = fiber.clock() + timeout
    local fibers = {}
    for i, job in ipairs(jobs) do
        fibers[i] = fiber.new(job)
        fibers[i]:set_joinable(true)
    end
    local first_err = nil
    for _, f in ipairs(fibers) do
        local ok, err = f:join(math.max(0, deadline - fiber.clock()))
        if f:status() ~= 'dead' then
            for _, other in ipairs(fibers) do
                -- A dead fiber can't be cancelled.
                if other:status() ~= 'dead' then
                    other:cancel()
                end
            end
            error(string.format('The parallel jobs are not done in %s ' ..
                                'seconds', timeout))
        end
        if not ok and first_err == nil then
            first_err = err
        end
    end
    if first_err ~= nil then
        error(first_err)
    end
end

-- Call func(item) for each item of the list, all the calls in parallel, see
-- run_in_parallel(). The timeout is the one of run_in_parallel().
function M.for_each_in_parallel(items, func, timeout)
    local jobs = {}
    for _, item in pairs(items) do
        table.insert(jobs, function()
            func(item)
        end)
    end
    M.run_in_parallel(jobs, timeout)
end

-- Drop the replica set like replica_set:drop() does, but with all the servers
-- stopping at once, not one after another. The timeout is the one of
-- run_in_parallel().
function M.drop_replica_set(replica_set, timeout)
    M.for_each_in_parallel(replica_set.servers, function(server)
        server:drop()
    end, timeout)
end

return M
