--
-- Helpers for the tests of VRaft - the elections and the limbo: a replica set
-- built for stepping through promotions, promotions stopped at a chosen WAL
-- write, and the waits for the outcomes.
--
-- The code running inside the servers takes its timeout from _G.TIMEOUT, set
-- by build_replica_set() when asked, or by the test itself.
--
local replica_link = require('test.replication-luatest.lib.replica_link')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')
local tools = require('test.replication-luatest.lib.tools')

local M = {}

-- The pause between the checks of a wait. What the tests wait for happens in
-- about a millisecond, usually right after the first check, and the default
-- pause of 0.1 seconds, taken dozens of times, was most of a test's run time.
local RETRY_DELAY = 0.01

-- Set what the code running inside the server relies on: _G.TIMEOUT and the
-- short pause between the checks of a wait. A restart loses both.
local function set_globals(node, glob_timeout)
    node:exec(function(timeout, retry_delay)
        rawset(_G, 'TIMEOUT', timeout)
        assert(t.helpers.RETRYING_DELAY ~= nil)
        t.helpers.RETRYING_DELAY = retry_delay
    end, {glob_timeout, RETRY_DELAY})
end

-- Let the blocked WAL writes of the node through one by one until the synchro
-- queue is busy with a write stuck in the WAL. The WAL delay must be armed
-- before the call. The same as the server's
-- play_wal_until_synchro_queue_is_busy(), but with the short pause between
-- the checks.
local function play_wal_until_queue_is_busy(node)
    node:exec(function(retry_delay)
        while true do
            t.helpers.retrying({
                timeout = _G.TIMEOUT, delay = retry_delay,
            }, function()
                t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
            end)
            if box.info.synchro.queue.busy then
                return
            end
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end
    end, {RETRY_DELAY})
end

-- A full mesh of opts.node_count nodes, node1..nodeN, each configured with
-- box_cfg plus the replication list. The nodes are in cg.nodes. With elections
-- on the set starts leaderless: an active leader doesn't issue new PROMOTEs.
--
-- opts.glob_timeout is set on every node as _G.TIMEOUT.
--
-- The mesh gets the replication timeout opts.replication_timeout, 1000 by
-- default - one never meant to fire. The tests hold acks and WAL writes, and
-- the silence must not break a connection. The bootstrap can't use the huge
-- timeout right away - its retries would be as rare - so the mesh is rebuilt
-- with it after the bootstrap, see reconnect_fullmesh(). The bootstrap's
-- box_cfg.replication_timeout stays as the reconnect timeout.
function M.build_replica_set(cg, box_cfg, opts)
    assert(opts.glob_timeout ~= nil)
    local replication_timeout = opts.replication_timeout or 1000
    cg.replica_set = replica_set:new{}
    box_cfg = table.copy(box_cfg)
    box_cfg.replication = {}
    for i = 1, opts.node_count do
        table.insert(box_cfg.replication,
                     server.build_listen_uri('node' .. i, cg.replica_set.id))
    end
    cg.nodes = {}
    for i = 1, opts.node_count do
        cg.nodes[i] = cg.replica_set:build_and_add_server{
            alias = 'node' .. i,
            box_cfg = box_cfg,
        }
    end
    -- The waits of the test itself, in this process and in the servers, get
    -- the short pause between the checks as their default.
    assert(t.helpers.RETRYING_DELAY ~= nil)
    t.helpers.RETRYING_DELAY = RETRY_DELAY
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh{delay = RETRY_DELAY}
    tools.for_each_in_parallel(cg.nodes, function(node)
        set_globals(node, opts.glob_timeout)
    end, opts.glob_timeout)
    M.reconnect_fullmesh(cg.nodes, {
        timeout = replication_timeout,
        reconnect_timeout = box_cfg.replication_timeout,
    })
    cg.replica_set:wait_for_fullmesh{delay = RETRY_DELAY}
    -- A restart starts the node with the server object's box_cfg, which the
    -- box.cfg() calls above don't touch: a restarted node must come back with
    -- the timeouts of the mesh, not the bootstrap's.
    for _, node in pairs(cg.nodes) do
        node.box_cfg.replication_timeout = replication_timeout
        node.box_cfg.replication_reconnect_timeout =
            box_cfg.replication_timeout
    end
    if box_cfg.election_mode == nil or box_cfg.election_mode == 'off' then
        return
    end
    tools.for_each_in_parallel(cg.nodes, function(node)
        node:exec(function()
            if box.info.election.state == 'leader' then
                box.ctl.demote()
            end
        end)
    end, opts.glob_timeout)
    tools.for_each_in_parallel(cg.nodes, function(node)
        t.helpers.retrying({
            timeout = opts.glob_timeout, delay = RETRY_DELAY,
        }, function()
            t.assert_equals(node:exec(function()
                return box.info.election.leader
            end), 0)
        end)
    end, opts.glob_timeout)
end

-- Restart the node and set on it again what build_replica_set() did and the
-- restart lost: glob_timeout as _G.TIMEOUT and the pause between the checks
-- of a wait.
function M.restart(node, glob_timeout)
    node:restart()
    set_globals(node, glob_timeout)
end

-- Point every node of the list at all of them, all the nodes at once. No
-- waiting: the caller knows what the connections are to bring.
function M.connect_fullmesh(nodes)
    local uris = {}
    for _, node in pairs(nodes) do
        table.insert(uris, node.net_box_uri)
    end
    tools.for_each_in_parallel(nodes, function(node)
        node:update_box_cfg{replication = uris}
    end)
end

-- Reconnect a running full mesh with new timeouts: opts.timeout becomes
-- replication_timeout, opts.reconnect_timeout becomes
-- replication_reconnect_timeout. In the only order that works: close the
-- replication on every node, set the timeouts on every node, reconnect the
-- mesh. A read in flight keeps the disconnect budget it started with, while
-- the heartbeats of the peer already come at the new pace - so the raise
-- needs fresh connections. And a relay started under the old timeout for a
-- peer already on the new one dies on its first wait for an ack - so both
-- ends raise before any connection is made. The reconnect timeout is to be
-- short: a subscribe racing the old relay's exit is refused as a duplicate
-- connection and must be retried soon. Each of the three steps is done on all
-- the nodes at once. No waiting: the caller waits for the mesh.
function M.reconnect_fullmesh(nodes, opts)
    assert(opts.timeout ~= nil)
    assert(opts.reconnect_timeout ~= nil)
    tools.for_each_in_parallel(nodes, function(node)
        node:update_box_cfg{replication = {}}
    end)
    tools.for_each_in_parallel(nodes, function(node)
        node:update_box_cfg{
            replication_timeout = opts.timeout,
            replication_reconnect_timeout = opts.reconnect_timeout,
        }
    end)
    M.connect_fullmesh(nodes)
end

-- A started link for the downstream node to replicate from the upstream node
-- through, both given by their numbers, with the given default timeout of its
-- waits. The test keeps the link and stops it after itself.
function M.make_link(cg, downstream, upstream, timeout)
    local link = replica_link.new{
        upstream = cg.nodes[upstream].net_box_uri,
        alias = string.format('link_%d_from_%d', downstream, upstream),
        replica_set_id = cg.replica_set.id,
        timeout = timeout,
    }
    link:start()
    return link
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
    play_wal_until_queue_is_busy(node)
end

-- Start a promotion whose PROMOTE lands and gets replicated, while its CONFIRM
-- gets blocked in the WAL. The WAL writes are let through one by one until the
-- own pending PROMOTE appears and the CONFIRM is being written: the worker
-- holds the limbo latch for the write, and the write is stuck in the WAL. A
-- stray row blocked ahead of it keeps it stuck as well.
function M.promote_start_blocked_at_confirm(node, fiber_name)
    node:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
    end)
    M.promote_start(node, fiber_name)
    node:exec(function(retry_delay)
        while true do
            t.helpers.retrying({
                timeout = _G.TIMEOUT, delay = retry_delay,
            }, function()
                t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
            end)
            if box.info.synchro.nodes[box.info.id] ~= nil and
               box.info.synchro.nodes[box.info.id].promote ~= nil and
               box.info.synchro.queue.busy then
                return
            end
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end
    end, {RETRY_DELAY})
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
-- voters to have nothing it lacks, so this precedes an election. All the
-- nodes wait at once.
function M.wait_sync(cg)
    tools.for_each_in_parallel(cg.nodes, function(node)
        for _, other in pairs(cg.nodes) do
            node:wait_for_vclock_of(other)
        end
    end)
end

-- Wait until the applier from the given upstream stops with a split-brain
-- error carrying the message. A stopped applier doesn't restart on its own, so
-- a wrong error is final - no point in waiting for it to change.
function M.wait_upstream_split_brain(node, upstream_id, message)
    node:exec(function(upstream_id, message, retry_delay)
        t.helpers.retrying({
            timeout = _G.TIMEOUT, delay = retry_delay,
        }, function()
            local upstream = box.info.replication[upstream_id].upstream
            t.assert_equals(upstream.status, 'stopped')
        end)
        local upstream = box.info.replication[upstream_id].upstream
        t.assert_str_contains(upstream.message, 'Split-Brain')
        t.assert_str_contains(upstream.message, message)
    end, {upstream_id, message, RETRY_DELAY})
end

-- Wait until every applier of the node is following. A replication
-- reconfiguration returns with the new upstreams connected, not yet synced.
function M.wait_for_all_upstreams_follow(node)
    node:exec(function(retry_delay)
        t.helpers.retrying({
            timeout = _G.TIMEOUT, delay = retry_delay,
        }, function()
            for _, replica in pairs(box.info.replication) do
                if replica.upstream ~= nil then
                    t.assert_equals(replica.upstream.status, 'follow')
                end
            end
        end)
    end, {RETRY_DELAY})
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
