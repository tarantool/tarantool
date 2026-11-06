local t = require('luatest')
local log = require('log')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')
local proxy = require('luatest.replica_proxy')

local g = t.group()

-- Stringify the proxy's internal state for the logs: whether it accepts new
-- connections and the running-flag of every connection it has.
local function proxy_str(p)
    local conns = {}
    for i, c in pairs(p.connections) do
        table.insert(conns, string.format('%s:%s', tostring(i),
                                          tostring(c.running)))
    end
    return string.format('accept=%s conns={%s}',
                         tostring(p.accept_new_connections),
                         table.concat(conns, ', '))
end

--
-- gh-8095: a node could win the elections, write a PROMOTE, and get cut off
-- the replication before the ownership-changing row would reach the other
-- nodes. Then another node could win the elections in a next term and claim
-- the queue for itself. When the old leader would return, its stale rows used
-- to be treated as a split-brain, breaking the replication on all the other
-- nodes.
--
-- Now a PROMOTE gathers a quorum of acks and gets applied only when its
-- CONFIRM arrives, like a synchronous transaction. The stale unconfirmed
-- promotion gets nopified by the other nodes after the proper validation, the
-- old leader discovers the new one and steps down, and the replication stays
-- intact.
--
-- The test doesn't hardcode which exact WAL row completes the queue claim (it
-- used to be the PROMOTE itself, now it is the CONFIRM after it). Instead it
-- walks the old leader's WAL writes one by one, hiding each row from the
-- replication until the row is fully processed locally. The row after whose
-- processing the node became the queue owner stays hidden. This way exactly
-- the ownership-changing row is cut off, whatever request it is.
--

--
-- Helpers to let a node's WAL writes through one by one, inspecting arbitrary
-- states in between. A good candidate for reuse in other tests.
--
-- Block the next WAL write before it lands into the journal. It will hang
-- with ERRINJ_WAL_DELAY set to true.
local function wal_arm(node)
    node:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
    end)
end

-- Wait until a WAL write is caught by the armed delay.
local function wal_wait_blocked(node)
    node:exec(function()
        t.helpers.retrying({timeout = 60}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
    end)
end

-- Let the currently blocked WAL write through and block the next one.
local function wal_step(node)
    node:exec(function()
        assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        require('log').info('gh-8095: release the blocked WAL row')
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
end

-- Let all the WAL writes through freely again.
local function wal_disarm(node)
    node:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
end

local function get_limbo_state(node)
    return node:exec(function()
        local queue = box.info.synchro.queue
        local state = {
            is_busy = queue.busy,
            owner = queue.owner,
            lsn = box.info.lsn,
        }
        require('log').info('gh-8095: limbo state: busy=%s owner=%s own_lsn=%s',
                            tostring(state.is_busy), tostring(state.owner),
                            tostring(state.lsn))
        return state
    end)
end

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new({})
    local rs_id = cg.replica_set.id
    local uri1 = server.build_listen_uri('server1', rs_id)
    local uri2 = server.build_listen_uri('server2', rs_id)
    local uri3 = server.build_listen_uri('server3', rs_id)
    local proxy_uri_1_to_2 = server.build_listen_uri('proxy_1_to_2', rs_id)
    local proxy_uri_1_to_3 = server.build_listen_uri('proxy_1_to_3', rs_id)
    local proxy_uri_2_to_1 = server.build_listen_uri('proxy_2_to_1', rs_id)
    local proxy_uri_3_to_1 = server.build_listen_uri('proxy_3_to_1', rs_id)
    --
    -- All the replication of server1, in both directions, goes via proxies,
    -- so the test can cut it off the cluster and heal it back at the exact
    -- moments it needs.
    --
    local box_cfg = {
        replication_timeout = 0.1,
        replication_synchro_timeout = 120,
        --
        -- The elections are disabled during the bootstrap, so the test fully
        -- controls the very first election and queue claim.
        --
        election_mode = 'off',
        --
        -- The old leader must keep believing in its leadership while being
        -- isolated. Otherwise it would resign right on the isolation, before
        -- the test heals the cluster.
        --
        election_fencing_mode = 'off',
    }
    box_cfg.replication = {uri1, proxy_uri_1_to_2, proxy_uri_1_to_3}
    cg.server1 = cg.replica_set:build_and_add_server({
        alias = 'server1',
        box_cfg = box_cfg,
    })
    box_cfg.replication = {proxy_uri_2_to_1, uri2, uri3}
    cg.server2 = cg.replica_set:build_and_add_server({
        alias = 'server2',
        box_cfg = box_cfg,
    })
    box_cfg.replication = {proxy_uri_3_to_1, uri2, uri3}
    cg.server3 = cg.replica_set:build_and_add_server({
        alias = 'server3',
        box_cfg = box_cfg,
    })
    cg.proxy_1_to_2 = proxy:new({
        client_socket_path = proxy_uri_1_to_2,
        server_socket_path = uri2,
    })
    cg.proxy_1_to_3 = proxy:new({
        client_socket_path = proxy_uri_1_to_3,
        server_socket_path = uri3,
    })
    cg.proxy_2_to_1 = proxy:new({
        client_socket_path = proxy_uri_2_to_1,
        server_socket_path = uri1,
    })
    cg.proxy_3_to_1 = proxy:new({
        client_socket_path = proxy_uri_3_to_1,
        server_socket_path = uri1,
    })
    t.assert(cg.proxy_1_to_2:start())
    t.assert(cg.proxy_1_to_3:start())
    t.assert(cg.proxy_2_to_1:start())
    t.assert(cg.proxy_3_to_1:start())
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    for _, s in pairs({cg.server1, cg.server2, cg.server3}) do
        s:exec(function()
            box.cfg{election_mode = 'manual'}
        end)
    end
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

g.test_stale_unconfirmed_promotion = function(cg)
    local server1_id = cg.server1:get_instance_id()
    local server2_id = cg.server2:get_instance_id()
    --
    -- Server1 starts claiming the queue. Its WAL writes are let through one
    -- by one, so no row can slip into the replication unnoticed.
    --
    wal_arm(cg.server1)
    cg.server1:exec(function()
        local fiber = require('fiber')
        local f = fiber.new(function()
            return pcall(box.ctl.promote)
        end)
        f:set_joinable(true)
        rawset(_G, 'promote_fiber', f)
    end)
    --
    -- Find the WAL row whose completion makes server1 the queue owner, and
    -- cut exactly it off the replication.
    --
    local step_count = 0
    while true do
        wal_wait_blocked(cg.server1)
        step_count = step_count + 1
        log.info('gh-8095: step %d: server1 has a blocked WAL row',
                 step_count)
        --
        -- The blocked row might be the one whose local completion makes
        -- server1 the owner - then it must not reach the other nodes. Hide
        -- it from them until it is clear what its completion does.
        --
        cg.proxy_2_to_1:pause()
        cg.proxy_3_to_1:pause()
        log.info('gh-8095: step %d: paused 2_to_1 {%s}, 3_to_1 {%s}',
                 step_count, proxy_str(cg.proxy_2_to_1),
                 proxy_str(cg.proxy_3_to_1))
        wal_step(cg.server1)
        --
        -- Every limbo state transition is done under the limbo latch, taken
        -- before its WAL write and released only when the row is fully
        -- applied. So once the limbo is seen not busy, the row's effect on
        -- the ownership, if any, is visible. The rows not touching the limbo
        -- (the election ones) can't change the ownership at all.
        --
        local state
        t.helpers.retrying({timeout = 60}, function()
            state = get_limbo_state(cg.server1)
            t.assert_not(state.is_busy)
        end)
        if state.owner == server1_id then
            --
            -- The claim is completed locally while its last row is trapped
            -- in the paused proxies. The wanted state.
            --
            log.info('gh-8095: step %d: server1 became the owner, the last '..
                     'row is trapped', step_count)
            break
        end
        --
        -- Not an ownership change. The row has to be replicated - the
        -- elections and the claim need the replication alive to proceed:
        -- the votes and the acks travel through these connections.
        --
        cg.proxy_2_to_1:resume()
        cg.proxy_3_to_1:resume()
        log.info('gh-8095: step %d: not an ownership change, resumed the '..
                 'proxies', step_count)
    end
    --
    -- Server1 considers itself the queue owner, but the other nodes don't
    -- know that. Isolate it fully now, so it doesn't learn about the next
    -- elections until the cluster is healed.
    --
    cg.proxy_1_to_2:pause()
    cg.proxy_1_to_3:pause()
    log.info('gh-8095: fully isolated server1: 1_to_2 {%s}, 1_to_3 {%s}',
             proxy_str(cg.proxy_1_to_2), proxy_str(cg.proxy_1_to_3))
    wal_disarm(cg.server1)
    local old_term = cg.server1:exec(function()
        local joined, ok, err = _G.promote_fiber:join(60)
        t.assert(joined)
        t.assert_equals(err, nil)
        t.assert(ok)
        local info = box.info
        t.assert_equals(info.synchro.queue.owner, info.id)
        t.assert_equals(info.election.state, 'leader')
        return info.election.term
    end)
    --
    -- A new leader is elected in a next term while the old one is isolated.
    -- Exactly the situation when the unreplicated claim of the old leader
    -- could cause a split-brain.
    --
    log.info('gh-8095: promoting server2')
    local new_term = cg.server2:exec(function()
        box.ctl.promote()
        local info = box.info
        t.assert_equals(info.synchro.queue.owner, info.id)
        t.assert_equals(info.election.state, 'leader')
        return info.election.term
    end)
    t.assert_gt(new_term, old_term)
    t.helpers.retrying({timeout = 60}, function()
        cg.server3:exec(function(owner_id, term)
            t.assert_equals(box.info.synchro.queue.owner, owner_id)
            t.assert_equals(box.info.election.term, term)
        end, {server2_id, new_term})
    end)
    -- The old leader meanwhile still lives in the past.
    cg.server1:exec(function(term)
        local info = box.info
        t.assert_equals(info.synchro.queue.owner, info.id)
        t.assert_equals(info.election.state, 'leader')
        t.assert_equals(info.election.term, term)
    end, {old_term})
    --
    -- The old leader returns. Its stale claim reaches the other nodes and
    -- must be nopified by them. The old leader must discover the new one and
    -- step down.
    --
    log.info('gh-8095: healing the cluster')
    cg.proxy_2_to_1:resume()
    cg.proxy_3_to_1:resume()
    cg.proxy_1_to_2:resume()
    cg.proxy_1_to_3:resume()
    --
    -- The stale rows of the old leader must physically go through everywhere.
    -- The vclocks can converge only when the other nodes accept (nopify)
    -- them. Rejecting the rows would stop the appliers and keep the vclocks
    -- apart forever, however hard the appliers would retry.
    --
    cg.server2:wait_for_vclock_of(cg.server1)
    cg.server3:wait_for_vclock_of(cg.server1)
    t.helpers.retrying({timeout = 60}, function()
        cg.server1:exec(function(owner_id, term)
            local info = box.info
            t.assert_equals(info.synchro.queue.owner, owner_id)
            t.assert_equals(info.election.term, term)
            t.assert_equals(info.election.state, 'follower')
        end, {server2_id, new_term})
    end)
    --
    -- The new leader is not affected, and the stale rows didn't break the
    -- replication anywhere.
    --
    t.helpers.retrying({timeout = 60}, function()
        cg.server2:exec(function(old_leader_id)
            local info = box.info
            t.assert_equals(info.synchro.queue.owner, info.id)
            t.assert_equals(info.election.state, 'leader')
            t.assert_equals(info.replication[old_leader_id].upstream.status,
                            'follow')
        end, {server1_id})
        cg.server3:exec(function(owner_id, old_leader_id)
            local info = box.info
            t.assert_equals(info.synchro.queue.owner, owner_id)
            t.assert_equals(info.replication[old_leader_id].upstream.status,
                            'follow')
        end, {server2_id, server1_id})
    end)
    --
    -- Smoke-check that the cluster is fully functional as one whole.
    --
    cg.server2:exec(function()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        s:insert{1}
    end)
    cg.server1:wait_for_vclock_of(cg.server2)
    cg.server3:wait_for_vclock_of(cg.server2)
    cg.server1:exec(function()
        t.assert_equals(box.space.test:get{1}, {1})
    end)
    cg.server3:exec(function()
        t.assert_equals(box.space.test:get{1}, {1})
    end)
end
