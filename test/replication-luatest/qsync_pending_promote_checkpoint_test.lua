local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

--
-- A pending PROMOTE is a part of the limbo state. It has to survive a
-- checkpoint, or the CONFIRM applying it would find nothing to apply on the
-- recovered instance, or on a replica bootstrapped from the checkpoint, and
-- the promotion would silently never happen there.
--
g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication = {
            server.build_listen_uri('node1', cg.replica_set.id),
            server.build_listen_uri('node2', cg.replica_set.id),
        },
        replication_timeout = 0.1,
        replication_synchro_quorum = 2,
        replication_synchro_timeout = 60,
        election_mode = 'manual',
        -- Node 2 restarts while node 1 is leading with a pending PROMOTE.
        -- Losing the only follower must not make node 1 step down.
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
    -- The bootstrap leader is the Raft leader already. An active leader
    -- doesn't issue new PROMOTEs. The tests need a leaderless start.
    for _, node in pairs(cg.nodes) do
        node:exec(function()
            rawset(_G, 'fiber', require('fiber'))
            if box.info.election.state == 'leader' then
                box.ctl.demote()
            end
        end)
    end
    for _, node in pairs(cg.nodes) do
        node:exec(function()
            t.helpers.retrying({timeout = 60}, function()
                t.assert_equals(box.info.election.leader, 0)
            end)
        end)
    end
    cg.node1, cg.node2 = unpack(cg.nodes)
    cg.node1_id = cg.node1:get_instance_id()
end)

g.after_each(function(cg)
    pcall(cg.node1.exec, cg.node1, function()
        box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', false)
    end)
    cg.replica_set:drop()
end)

-- Node 1 gets elected and writes its PROMOTE, but its CONFIRM is held back.
-- The PROMOTE stays pending everywhere.
local function promote_hold_confirm(cg)
    cg.node1:exec(function()
        box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', true)
        local f = _G.fiber.new(box.ctl.promote)
        f:set_joinable(true)
        rawset(_G, 'promote_fiber', f)
    end)
    cg.node1:exec(function()
        t.helpers.retrying({timeout = 60}, function()
            t.assert_equals(box.info.election.state, 'leader')
            t.assert_not_equals(box.info.synchro.own_promote, nil)
        end)
    end)
    cg.term = cg.node1:get_election_term()
    for _, node in pairs(cg.nodes) do
        node:wait_for_vclock_of(cg.node1)
    end
end

local function promote_release_confirm(cg)
    cg.node1:exec(function()
        box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', false)
        t.assert(rawget(_G, 'promote_fiber'):join(60))
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end)
end

local function assert_pending_promote(node, cg)
    node:exec(function(node1_id, term)
        local promote = box.info.synchro.nodes[node1_id].promote
        t.assert_equals(promote.term, term)
        t.assert_not(promote.is_poisoned)
        t.assert_lt(box.info.synchro.queue.term, term)
    end, {cg.node1_id, cg.term})
end

local function wait_promote_applied(node, cg)
    node:exec(function(node1_id, term)
        t.helpers.retrying({timeout = 60}, function()
            t.assert_equals(box.info.synchro.queue.term, term)
        end)
        t.assert_equals(box.info.synchro.queue.owner, node1_id)
        t.assert_equals(box.info.synchro.nodes[node1_id].promote, nil)
    end, {cg.node1_id, cg.term})
end

g.test_pending_survives_restart = function(cg)
    promote_hold_confirm(cg)
    assert_pending_promote(cg.node2, cg)
    cg.node2:exec(function()
        box.snapshot()
    end)
    cg.node2:restart()
    assert_pending_promote(cg.node2, cg)
    promote_release_confirm(cg)
    wait_promote_applied(cg.node2, cg)
end

g.test_pending_survives_join = function(cg)
    promote_hold_confirm(cg)
    -- Nobody is writable while the promotion is in progress, so a new replica
    -- can't get registered. An anonymous one needs no registration, and gets
    -- the limbo state like any other.
    local box_cfg = table.copy(cg.node1.box_cfg)
    box_cfg.replication_anon = true
    box_cfg.read_only = true
    box_cfg.election_mode = 'off'
    cg.node3 = cg.replica_set:build_and_add_server{
        alias = 'node3',
        box_cfg = box_cfg,
    }
    cg.node3:start()
    table.insert(cg.nodes, cg.node3)
    assert_pending_promote(cg.node3, cg)
    promote_release_confirm(cg)
    wait_promote_applied(cg.node3, cg)
end
