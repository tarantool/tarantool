local nb = require('net.box')
local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new{box_cfg = {
        memtx_use_mvcc_engine = true,
        replication_synchro_timeout = 120,
        replication_synchro_quorum = 2,
    }}
    cg.server:start()
    cg.server:exec(function()
        box.schema.create_space('async', {is_sync = false}):create_index('pk')
        box.ctl.promote()
    end)
end)

g.test_box_begin_is_sync_sync_commit_after_recovery = function(cg)
    local c = nb.connect(cg.server.net_box_uri)
    local s = c:new_stream()
    s:begin{is_sync = true}
    -- The transaction meta flags are only assigned to the last transaction
    -- statement, so let's test that this is accounted for by adding 2
    -- statements rather than 1.
    s.space.async:replace{0}
    s.space.async:replace{1}
    s:commit({is_async = true})

    cg.server:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
        end)
    end)

    c:close()

    cg.server:restart()

    cg.server:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.space.async:get{0}, nil)
        t.assert_equals(box.space.async:get{1}, nil)
    end)
end

g.after_each(function(cg)
    cg.server:drop()
end)
