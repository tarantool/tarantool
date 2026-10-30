local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group('gh_10047')
local wait_timeout = 10

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new({})
    cg.replication = {
        server.build_listen_uri('server1', cg.replica_set.id),
    }
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = {
            replication_timeout = 0.1,
        },
    }
    cg.server2 = cg.replica_set:build_and_add_server{
        alias = 'server2',
        box_cfg = {
            replication_timeout = 0.1,
            replication = server.build_listen_uri('server1', cg.replica_set.id),
        },
    }
    cg.replica_set:start()
    cg.server1:exec(function()
        local s1 = box.schema.create_space('test')
        s1:create_index('pk')
        local s2 = box.schema.create_space('test_loc', {is_local = true})
        s2:create_index('pk')
    end)
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

--
-- gh-10047: relay used to save local vclock[0] as the last received ACK from
-- the replica which just subscribed and didn't send any real ACKs. When a real
-- ACK was received, it didn't have vclock[0] and it looked like vclock[0] went
-- backwards. That broke an assert in relay.
--
g.test_downstream_vclock_no_local = function(cg)
    -- Make sure there is a local row on master and vclock isn't empty.
    cg.server1:exec(function()
        box.space.test:replace{1}
        box.space.test_loc:replace{1}
    end)
    cg.server2:wait_for_vclock_of(cg.server1)
    local server2_id = cg.server2:get_instance_id()
    cg.server2:stop()
    cg.server1:exec(function()
        -- On restart the replica's ACKs are not received for a while. Need to
        -- catch the moment when the subscribe vclock appears in
        -- info.replication and it isn't yet overridden by a real ACK.
        box.error.injection.set("ERRINJ_RELAY_READ_ACK_DELAY", true)
        -- While the replica is away, the master moves a bit. To make replica's
        -- vclock smaller so as it would receive actual data after subscribe and
        -- send a real ACK (not just an empty heartbeat).
        box.space.test:replace{2}
    end)
    cg.server2:start()
    cg.server1:exec(function(id, timeout)
        local fiber = require('fiber')
        -- Wait until subscribe is done.
        t.helpers.retrying({timeout = timeout}, function()
            local info = box.info.replication[id]
            if info and info.downstream and info.downstream.vclock and
               next(info.downstream.vclock) then
                t.assert_equals(info.downstream.vclock[0], nil)
                return
            end
            error("No subscribe from the replica")
        end)
        -- When subscribe is just finished, relay has subscribe vclock saved as
        -- last-ACK. But the crash was triggered when before-last-ACK was >=
        -- last-ACK. The latter becomes the former when relay exchanges status
        -- messages with TX thread.
        --
        -- Hence need to wait until the TX thread gets notified by the relay
        -- about anything.
        t.helpers.retrying({timeout = timeout}, function()
            local ack_period = box.cfg.replication_timeout
            fiber.sleep(ack_period)
            local info = box.info.replication[id]
            if info and info.downstream and info.downstream.idle and
               info.downstream.idle < ack_period then
                return
            end
            error("No report to TX thread")
        end)
        -- Receive a real ACK. It must be >= subscribe vclock or master dies.
        box.error.injection.set("ERRINJ_RELAY_READ_ACK_DELAY", false)
    end, {server2_id, wait_timeout})
    cg.server2:wait_for_vclock_of(cg.server1)
end
