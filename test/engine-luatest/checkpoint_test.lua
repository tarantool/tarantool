local t = require('luatest')
local server = require('luatest.server')
local xlog = require('xlog')

local g = t.group()

g.before_all(function(g)
    g.server = server:new()
    g.server:start()
    g.server:exec(function()
        box.schema.space.create('test'):create_index('pk')
    end)
end)

g.after_all(function(g)
    g.server:drop()
end)

--
-- Test, that the limbo and raft states are saved after
-- system spaces but before user data.
--
g.test_synchro_states_checkpoint = function(g)
    local lsn = g.server:exec(function()
        box.space.test:insert{1, 'data'}
        box.snapshot()
        return box.info.vclock[1]
    end)

    -- Read the whole snap in memory, as otherwise
    -- it's impossible to access data by index.
    local data = {}
    local snap_template = '%020d.snap'
    local snap = g.server.workdir .. '/' .. string.format(snap_template, lsn)
    for _, v in xlog.pairs(snap) do
        table.insert(data, v)
    end

    -- Skip data from the system spaces.
    local state_idx = 0
    for i, row in ipairs(data) do
        if row.HEADER.type ~= 'INSERT' then
            state_idx = i
            break
        end
    end
    -- The next rows are raft and limbo states.
    t.assert_equals(data[state_idx].HEADER.type, 'RAFT')
    t.assert_equals(data[state_idx + 1].HEADER.type, 'RAFT_PROMOTE')
    -- And after states there's some user data.
    t.assert_equals(data[state_idx + 2].HEADER.type, 'INSERT')
    t.assert_ge(data[state_idx + 2].BODY.space_id, 512)
end
