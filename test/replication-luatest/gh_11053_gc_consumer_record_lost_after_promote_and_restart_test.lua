local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group()

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    cg.box_cfg = {
        replication = {
            server.build_listen_uri('master', cg.replica_set.id),
            server.build_listen_uri('replica', cg.replica_set.id),
        },
        replication_timeout = 0.1,
        replication_synchro_timeout = 120,
        replication_synchro_quorum = 4,
    }
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = cg.box_cfg,
    }
    cg.replica = cg.replica_set:build_and_add_server{
        alias = 'replica',
        box_cfg = cg.box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function()
        box.schema.space.create('s', {is_sync = true}):create_index('p')
        box.ctl.promote()
    end)
    cg.master:wait_for_downstream_to(cg.replica)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.test_gc_consumer_record_not_lost_after_promote_and_restart = function(cg)
    -- Simulate network partitioning.
    cg.replica:update_box_cfg{replication = ''}
    -- Submit a synchronous transaction while being partitioned from replica.
    cg.master:exec(function()
        box.atomic({wait = 'submit'}, function() box.space.s:replace{0} end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.space._gc_consumers:len(), 1)
    end)
    -- Join a new replica.
    cg.new_replica = cg.replica_set:build_and_add_server{
        alias = 'new_replica',
        box_cfg = cg.box_cfg,
    }
    -- The join cannot be processed completely because we have a pending
    -- transaction in the synchronous queue.
    cg.new_replica:start({wait_until_ready = false})
    -- Wait for the new replica's `_gc_consumer` record to appear on the master.
    cg.master:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.space._gc_consumers:len(), 2)
        end)
    end)
    -- Promote the partitioned replica.
    cg.replica:exec(function()
        box.ctl.promote()
    end)
    -- Wait for the old master to receive promote from the partitioned replica.
    cg.master:exec(function(replica_id)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.owner, replica_id)
        end)
        t.assert_equals(box.info.synchro.queue.len, 0)
    end, {cg.replica:get_instance_id()})
    -- Restart the old master.
    cg.master:restart()
    -- Check that the new replica's `_gc_consumer` record is still present on
    -- the old master.
    cg.master:exec(function()
        t.assert_equals(box.space._gc_consumers:len(), 2)
    end)
end
