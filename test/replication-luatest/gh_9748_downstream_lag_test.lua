local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group('gh_9748')
local delay = 0.1
local wait_timeout = 60

g.before_all(function(cg)
    cg.replica_set = replica_set:new({})
    cg.replication = {
        server.build_listen_uri('server1', cg.replica_set.id),
        server.build_listen_uri('server2', cg.replica_set.id),
        server.build_listen_uri('server3', cg.replica_set.id),
    }
    local box_cfg = {
        replication = cg.replication,
        replication_timeout = 0.1,
    }
    for i = 1, 3 do
        cg['server'..i] = cg.replica_set:build_and_add_server{
            alias = 'server' .. i,
            box_cfg = box_cfg,
        }
    end
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.server1:exec(function()
        box.schema.create_space('test'):create_index('pk')
    end)
end)

g.before_each(function(cg)
    for _, s in pairs(cg.replica_set.servers) do
        s:update_box_cfg{replication = cg.replication}
    end
    cg.server1:wait_for_downstream_to(cg.server2)
    cg.server1:wait_for_downstream_to(cg.server3)
    cg.server2:wait_for_downstream_to(cg.server1)
    cg.server2:wait_for_downstream_to(cg.server3)
    cg.server3:wait_for_downstream_to(cg.server1)
    cg.server3:wait_for_downstream_to(cg.server2)
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

--
-- gh-9748: applier on reconnect must not send an old txn timestamp. Relay on
-- the other side might think it was an actual confirmation of an old txn, and
-- would ruin the downstream lag.
--
g.test_lag_on_master_restart = function(cg)
    cg.server1:exec(function()
        box.space.test:replace{1}
    end)
    cg.server2:wait_for_vclock_of(cg.server1)
    cg.server1:stop()
    -- To make vclock different. In case it would matter on the server1 side
    -- anyhow.
    cg.server2:exec(function()
        box.space.test:replace{2}
    end)
    cg.server1:start()
    t.helpers.retrying({timeout = wait_timeout}, function()
        cg.server2:assert_follows_upstream(cg.server1:get_instance_id())
    end)
    cg.server1:exec(function(id)
        t.assert_equals(box.info.replication[id].downstream.lag, 0)
    end, {cg.server2:get_instance_id()})
end

--
-- gh-9748: downstream lag must be updated even when the replica confirmed a txn
-- not created by the master. The lag represents WAL delay, and WAL shouldn't
-- care about replica IDs in the txns.
--
local function test_lag_from_third_node(cg)
    -- server1 <-> server2  |  server3
    cg.server3:update_box_cfg{replication = {}}
    -- Server2 creates downstream lag to server3.
    cg.server2:exec(function(delay)
        box.space.test:replace{1}
        require('fiber').sleep(delay)
    end, {delay})
    cg.server3:update_box_cfg{replication = {cg.replication[2]}}
    cg.server3:wait_for_vclock_of(cg.server2)
    cg.server2:exec(function(id, delay)
        t.assert_ge(box.info.replication[id].downstream.lag, delay)
    end, {cg.server3:get_instance_id(), delay})
    -- Server1 makes a txn, replicated to server3 via server2. But that still
    -- bumps the downstream lag in server2->server3. Downstream lag is updated
    -- even when the replica confirms third-node txns, not only master's own
    -- txns.
    cg.server1:exec(function()
        box.space.test:replace{2}
    end)
    cg.server2:wait_for_downstream_to(cg.server3)
    cg.server2:exec(function(id, delay)
        local lag = box.info.replication[id].downstream.lag
        t.assert_le(lag, delay)
        t.assert_not_equals(lag, 0)
    end, {cg.server3:get_instance_id(), delay})
end

g.test_lag_from_third_node = function(cg)
    -- Retry, because with a non-huge replication timeout the replicas sometimes
    -- might timeout when the system is slow, and that would make downstream lag
    -- disappear, breaking the test.
    t.helpers.retrying({timeout = wait_timeout}, test_lag_from_third_node, cg)
end

--
-- The test ensures the downstream lag uses the timestamp of when the txn was
-- written to WAL of the master, not when it was created. Those moments are
-- different for txns received by the considered master from another master, and
-- then replicated further.
--
local function test_lag_is_local_to_sender(cg)
    cg.server3:update_box_cfg{replication = {}}
    cg.server2:update_box_cfg{replication = {}}
    -- Create a lag in server1->server2.
    cg.server1:exec(function()
        box.space.test:replace{1}
    end)
    cg.server2:exec(function(delay, replication)
        require('fiber').sleep(delay)
        box.cfg{replication = replication}
    end, {delay, {cg.replication[1], cg.replication[2]}})
    cg.server1:wait_for_downstream_to(cg.server2)
    -- server1 -> server2 -> server3
    cg.server3:update_box_cfg{replication = {cg.replication[2]}}
    cg.server2:wait_for_downstream_to(cg.server3)
    -- Server2 has a low lag to server3.
    cg.server2:exec(function(id, delay)
        local lag = box.info.replication[id].downstream.lag
        t.assert_le(lag, delay)
        t.assert_not_equals(lag, 0)
    end, {cg.server3:get_instance_id(), delay})
    -- But server1->server2 lag is high. This proves, that the lag is calculated
    -- relative to when the master wrote the txn to its local WAL. Not relative
    -- to when the txn was created.
    cg.server1:exec(function(id, delay)
        t.assert_ge(box.info.replication[id].downstream.lag, delay)
    end, {cg.server2:get_instance_id(), delay})
end

g.test_lag_is_local_to_sender = function(cg)
    -- Retry, because with a non-huge replication timeout the replicas sometimes
    -- might timeout when the system is slow, and that would make downstream lag
    -- disappear, breaking the test.
    t.helpers.retrying({timeout = wait_timeout}, test_lag_is_local_to_sender,
                       cg)
end

--
-- Replica can be replicating from multiple masters. Getting data from one of
-- the masters shouldn't affect its downstream lag on the other masters.
--
g.test_lag_no_update_when_replica_follows_third_node = function(cg)
    -- server1 -> server2 <- server3
    cg.server1:update_box_cfg{replication = {}}
    cg.server3:update_box_cfg{replication = {}}
    -- Lag server1->server2 won't change if server2 is receiving txns from other
    -- nodes, bypassing server1. Because the lag represents delay between
    -- server1 and server2 WALs. When they are in sync, and server1 WAL doesn't
    -- change, the lag also shouldn't change.
    local lag = cg.server1:exec(function(id)
        return box.info.replication[id].downstream.lag
    end, {cg.server2:get_instance_id()})
    local vclock = cg.server3:exec(function()
        box.space.test:replace{1}
        return box.info.vclock
    end)
    -- Ignore local lsn
    vclock[0] = nil
    cg.server1:exec(function(id, vclock, lag, timeout)
        t.helpers.retrying({timeout = timeout}, function()
            require('log').info(box.info.replication[id].downstream)
            t.assert_equals(box.info.replication[id].downstream.vclock, vclock,
                            'Server2 did not ack server3 vclock to server1')
        end)
        t.assert_equals(box.info.replication[id].downstream.lag, lag)
    end, {cg.server2:get_instance_id(), vclock, lag, wait_timeout})
end
