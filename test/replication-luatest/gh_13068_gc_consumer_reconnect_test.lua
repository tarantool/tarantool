local t = require('luatest')
local replica_set = require('luatest.replica_set')
local fio = require('fio')

local g = t.group()

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = {checkpoint_count = 1, wal_cleanup_delay = 0},
    }
    for _, alias in ipairs({'relay', 'replica'}) do
        cg[alias] = cg.replica_set:build_and_add_server{
            alias = alias,
            box_cfg = {
                replication = cg.master.net_box_uri,
                read_only = true,
                bootstrap_strategy = 'config',
                bootstrap_leader = cg.master.net_box_uri,
            },
        }
    end
    cg.replica_set:start()
    cg.master:exec(function()
        box.schema.space.create('test'):create_index('pk')
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.relay:wait_for_vclock_of(cg.master)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.test_gc_consumer_reconnect = function(cg)
    -- Keep receiving updates through another node while disconnected from
    -- the master, so the replica's saved consumer becomes outdated.
    cg.replica:update_box_cfg{replication = cg.relay.net_box_uri}
    cg.master:exec(function(id)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.replication[id].downstream.status,
                            'stopped')
        end)
    end, {cg.replica:get_instance_id()})
    local replica_uuid = cg.replica:get_instance_uuid()
    local old_vclock = cg.master:exec(function(uuid)
        return box.space._gc_consumers:get{uuid}.vclock
    end, {replica_uuid})
    local old_xlogs = fio.glob(fio.pathjoin(cg.master.workdir, '*.xlog'))
    t.assert_gt(#old_xlogs, 0)

    cg.master:exec(function()
        box.space.test:replace{1}
        box.snapshot()
        -- Open the next WAL so the relay node can advance its consumer.
        box.space.test:replace{2}
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    local vclock = cg.replica:get_vclock()
    vclock[0] = nil
    cg.master:exec(function(uuid, old_vclock, relay_uuid)
        t.assert_equals(box.space._gc_consumers:get{uuid}.vclock, old_vclock)
        local checkpoint = box.info.gc().checkpoints[1].vclock
        t.helpers.retrying({}, function()
            local consumer = box.space._gc_consumers:get{relay_uuid}
            t.assert_ge(consumer.vclock[box.info.id],
                        checkpoint[box.info.id])
        end)
    end, {replica_uuid, old_vclock, cg.relay:get_instance_uuid()})
    for _, path in ipairs(old_xlogs) do
        t.assert(fio.path.exists(path))
    end

    cg.replica:update_box_cfg{replication = cg.master.net_box_uri}
    cg.master:wait_for_downstream_to(cg.replica)
    cg.master:exec(function(uuid, vclock)
        local consumer = box.space._gc_consumers:get{uuid}
        t.assert_equals(consumer.vclock, vclock)
        for _, consumer in ipairs(box.info.gc().consumers) do
            if consumer.name == 'replica ' .. uuid then
                local consumer_vclock = consumer.vclock
                consumer_vclock[0] = nil
                t.assert_equals(consumer_vclock, vclock)
                return
            end
        end
        t.fail('Replica GC consumer is missing')
    end, {replica_uuid, vclock})
    -- Reconnecting must release the old WALs without another rotation.
    t.helpers.retrying({}, function()
        for _, path in ipairs(old_xlogs) do
            t.assert_not(fio.path.exists(path))
        end
    end)
end
