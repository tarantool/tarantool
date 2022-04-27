#!/usr/bin/env tarantool

-- Instance file for one of the 10 swarm nodes. They bootstrap the cluster, each
-- bump their vclock component and then do nothing and serve as replication
-- masters for the eleventh node.

local id = tonumber(arg[1])
assert(id ~= nil, 'Please pass a numeric instance id')
assert(id >= 2 and id <= 11, 'The id should be in ramge [2, 11]')

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
    background = true,
    work_dir = tostring(id),
    pid_file = id..'.pid',
    log = id..'.log',
}

box.once('bootstrap', function()
    box.schema.user.grant('guest', 'replication')
end)

-- This is executed on every instance so that vclock is non-empty in each
-- component. This will make the testing instance copy a larger portion of data
-- on each write and make the performance degradation, if any, more obvious.
box.space._schema:replace{'Something to bump vclock '..id}
