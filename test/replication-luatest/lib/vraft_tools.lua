--
-- Helpers for the tests of VRaft - the elections and the limbo: a replica set
-- built for stepping through promotions, promotions stopped at a chosen WAL
-- write, and the waits for the outcomes.
--
-- The code running inside the servers takes its timeout from _G.TIMEOUT, set
-- by build_replica_set() when asked, or by the test itself.
--
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local M = {}

-- A full mesh of opts.node_count nodes, node1..nodeN, each configured with
-- box_cfg plus the replication list. The nodes are in cg.nodes, their URIs in
-- cg.uris - to cut nodes off the mesh and heal it back. With elections on the
-- set starts leaderless: an active leader doesn't issue new PROMOTEs.
--
-- opts.glob_timeout, when given, is set on every node as _G.TIMEOUT.
function M.build_replica_set(cg, box_cfg, opts)
    cg.replica_set = replica_set:new{}
    box_cfg = table.copy(box_cfg)
    box_cfg.replication = {}
    for i = 1, opts.node_count do
        table.insert(box_cfg.replication,
                     server.build_listen_uri('node' .. i, cg.replica_set.id))
    end
    cg.uris = table.copy(box_cfg.replication)
    cg.nodes = {}
    for i = 1, opts.node_count do
        cg.nodes[i] = cg.replica_set:build_and_add_server{
            alias = 'node' .. i,
            box_cfg = box_cfg,
        }
    end
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    if opts.glob_timeout ~= nil then
        for _, node in pairs(cg.nodes) do
            node:exec(function(timeout)
                rawset(_G, 'TIMEOUT', timeout)
            end, {opts.glob_timeout})
        end
    end
    if box_cfg.election_mode == nil or box_cfg.election_mode == 'off' then
        return
    end
    for _, node in pairs(cg.nodes) do
        node:exec(function()
            if box.info.election.state == 'leader' then
                box.ctl.demote()
            end
        end)
    end
    for _, node in pairs(cg.nodes) do
        t.helpers.retrying({timeout = opts.glob_timeout}, function()
            t.assert_equals(node:exec(function()
                return box.info.election.leader
            end), 0)
        end)
    end
end

-- Start a promotion in a fiber. The fiber is kept in the server's global of
-- the given name, for promote_join() to join it.
function M.promote_start(node, fiber_name)
    node:exec(function(fiber_name)
        local f = require('fiber').new(box.ctl.promote)
        f:set_joinable(true)
        rawset(_G, fiber_name, f)
    end, {fiber_name})
end

-- Start a promotion whose PROMOTE gets blocked in the WAL. The new term is
-- broadcast right away nonetheless.
function M.promote_start_wal_blocked(node, fiber_name)
    node:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
    end)
    M.promote_start(node, fiber_name)
    node:play_wal_until_synchro_queue_is_busy()
end

-- Start a promotion whose PROMOTE lands and gets replicated, while its CONFIRM
-- gets blocked in the WAL. The WAL writes are let through one by one until the
-- own pending PROMOTE appears - the write blocked after it is the CONFIRM.
function M.promote_start_blocked_at_confirm(node, fiber_name)
    node:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
    end)
    M.promote_start(node, fiber_name)
    node:exec(function()
        while true do
            t.helpers.retrying({timeout = _G.TIMEOUT}, function()
                t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
            end)
            if box.info.synchro.nodes[box.info.id] ~= nil and
               box.info.synchro.nodes[box.info.id].promote ~= nil then
                return
            end
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end
    end)
end

-- Finish the promotion started by one of the functions above, in the fiber of
-- the given name. Return ok, err as the join does - the promotion's result
-- and its error. The WAL sync makes sure all the promotion's rows have landed
-- even when the promotion gave up earlier.
function M.promote_join(node, fiber_name)
    return node:exec(function(fiber_name)
        local ok, err = rawget(_G, fiber_name):join(_G.TIMEOUT)
        rawset(_G, fiber_name, nil)
        box.ctl.wal_sync()
        return ok, err
    end, {fiber_name})
end

-- Release the blocked WAL write and finish the promotion, like
-- promote_join().
function M.promote_join_at_wal_release(node, fiber_name)
    node:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    return M.promote_join(node, fiber_name)
end

-- Wait until every node has every other node's rows. A candidate needs the
-- voters to have nothing it lacks, so this precedes an election.
function M.wait_sync(cg)
    for _, node in pairs(cg.nodes) do
        for _, other in pairs(cg.nodes) do
            node:wait_for_vclock_of(other)
        end
    end
end

-- Wait until the applier from the given upstream stops with a split-brain
-- error carrying the message. A stopped applier doesn't restart on its own, so
-- a wrong error is final - no point in waiting for it to change.
function M.wait_upstream_split_brain(node, upstream_id, message)
    node:exec(function(upstream_id, message)
        t.helpers.retrying({timeout = _G.TIMEOUT}, function()
            local upstream = box.info.replication[upstream_id].upstream
            t.assert_equals(upstream.status, 'stopped')
        end)
        local upstream = box.info.replication[upstream_id].upstream
        t.assert_str_contains(upstream.message, 'Split-Brain')
        t.assert_str_contains(upstream.message, message)
    end, {upstream_id, message})
end

-- Check that every applier of the node is following - nothing got refused.
function M.assert_all_upstreams_follow(node)
    node:exec(function()
        for id, replica in pairs(box.info.replication) do
            if id ~= box.info.id then
                t.assert_equals(replica.upstream.status, 'follow')
            end
        end
    end)
end

return M
