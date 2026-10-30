local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group()

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication = {
            server.build_listen_uri('remaining', cg.replica_set.id),
            server.build_listen_uri('deleted', cg.replica_set.id),
        },
        replication_timeout = 0.1,
    }
    cg.remaining = cg.replica_set:build_and_add_server{
        alias = 'remaining',
        box_cfg = box_cfg,
    }
    box_cfg.read_only = true
    cg.deleted = cg.replica_set:build_and_add_server{
        alias = 'deleted',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.remaining:exec(function()
        box.schema.space.create('s'):create_index('p')
    end)
    cg.deleted:exec(function()
        box.cfg{read_only = false}
    end)
    cg.replica_set:wait_for_fullmesh()
    cg.remaining:wait_for_downstream_to(cg.deleted)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

-- Test that asynchronous transactions from a deleted replica do not arrive on
-- the remaining replica.
g.test_async_tx_on_deleted_replica_do_not_arrive_on_remaining_replica =
function(cg)
    cg.remaining:exec(function()
        t.assert_equals(box.info.id, 1)
        box.space._cluster:delete{2}
    end)
    cg.deleted:wait_for_vclock_of(cg.remaining)
    cg.deleted:exec(function()
        t.assert_equals(box.info.id, nil)
        box.space.s:replace{0}
    end)
    t.assert_not_equals(cg.deleted:grep_log('exiting the relay loop'), nil)
    cg.remaining:exec(function(timeout)
        require('fiber').sleep(timeout)
        t.assert_equals(box.space.s:get{0}, nil)
    end, {1})
end
