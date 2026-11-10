local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group()

local TIMEOUT = 60

--
-- gh-13625: a new leader whose promotion can't complete has to shut down like
-- any other node. The Raft worker is cancelled inside the limbo's wait: the
-- wait returns an error, and the worker must not go to sleep again on its way
-- out.
--

g.before_all(function()
    t.tarantool.skip_if_not_debug()
end)

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    cg.uris = {
        server.build_listen_uri('node1', cg.replica_set.id),
        server.build_listen_uri('node2', cg.replica_set.id),
    }
    local box_cfg = {
        replication = cg.uris,
        replication_timeout = 0.1,
        replication_synchro_quorum = 2,
        replication_synchro_timeout = TIMEOUT,
        election_mode = 'manual',
        election_fencing_mode = 'off',
    }
    cg.nodes = {}
    for i = 1, 2 do
        cg.nodes[i] = cg.replica_set:build_and_add_server{
            alias = 'node' .. i,
            box_cfg = box_cfg,
        }
    end
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    -- The held acks must not break the connections. For that the timeout needs
    -- to be very high. And to change a timeout the replication has to be
    -- restarted. Bootstrap with a huge timeout might stall, because it doesn't
    -- use replication_reconnect_timeout.
    for _, node in pairs(cg.nodes) do
        node:update_box_cfg{replication = {}}
    end
    for _, node in pairs(cg.nodes) do
        node:update_box_cfg{
            replication_timeout = TIMEOUT,
            replication_reconnect_timeout = 0.1,
        }
    end
    local node1, node2 = unpack(cg.nodes)
    node1:update_box_cfg{replication = {cg.uris[2]}}
    node2:update_box_cfg{replication = {cg.uris[1]}}
    local node1_id, node2_id = node1:get_instance_id(), node2:get_instance_id()
    t.helpers.retrying({timeout = TIMEOUT}, function()
        node1:assert_follows_upstream(node2_id)
        node2:assert_follows_upstream(node1_id)
    end)
end)

g.after_each(function(cg)
    for _, node in pairs(cg.nodes) do
        pcall(node.exec, node, function()
            box.error.injection.set('ERRINJ_APPLIER_WRITER_DELAY', false)
        end)
    end
    cg.replica_set:drop()
end)

g.test_shutdown_while_promotion_waits = function(cg)
    local node1, node2 = unpack(cg.nodes)
    local node1_id = node1:get_instance_id()

    node1:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.schema.space.create('s', {is_sync = true}):create_index('p')
    end)
    node2:wait_for_vclock_of(node1)
    -- The owner's transaction reaches node 2, whose ack is held: it waits in
    -- the queue on both.
    node2:exec(function()
        box.error.injection.set('ERRINJ_APPLIER_WRITER_DELAY', true)
    end)
    node1:exec(function(timeout)
        require('fiber').new(function()
            box.space.s:replace{1}
        end)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
        end)
    end, {TIMEOUT})
    node2:wait_for_vclock_of(node1)
    node2:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 1)
    end)
    -- Node 2 takes the term with node 1's vote, node 1's acks held too. The
    -- leadership is there, the queue is not: the promotion waits.
    node1:exec(function()
        box.error.injection.set('ERRINJ_APPLIER_WRITER_DELAY', true)
    end)
    node2:exec(function(node1_id, timeout)
        require('fiber').new(box.ctl.promote)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.info.election.state, 'leader')
        end)
        t.assert_equals(box.info.synchro.queue.owner, node1_id)
        t.assert(box.info.ro)
    end, {node1_id, TIMEOUT})
    -- The Raft worker fiber needs to notice the cancel.
    node2.process:kill()
    t.helpers.retrying({timeout = TIMEOUT}, function()
        t.assert_not(node2.process:is_alive())
    end)
end
