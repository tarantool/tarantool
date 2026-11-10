local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')
local proxy = require('luatest.replica_proxy')
local vraft = require('test.replication-luatest.lib.vraft_tools')

local TIMEOUT = 60

local g = t.group()

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
-- The old leader's claim is stopped at the CONFIRM write - the row whose
-- completion makes it the queue owner. The PROMOTE before it is replicated and
-- acked, the CONFIRM lands only after the node is cut off.
--

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
        s:exec(function(timeout)
            rawset(_G, 'TIMEOUT', timeout)
            box.cfg{election_mode = 'manual'}
        end, {TIMEOUT})
    end
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

g.test_stale_unconfirmed_promotion = function(cg)
    local server1_id = cg.server1:get_instance_id()
    local server2_id = cg.server2:get_instance_id()
    --
    -- Server1 claims the queue. The PROMOTE is replicated and acked, the
    -- CONFIRM is stuck in the WAL.
    --
    vraft.promote_start_blocked_at_confirm(cg.server1, 'promote_fiber')
    --
    -- Isolate server1 fully: the CONFIRM must not reach the other nodes, and
    -- server1 must not learn about the next elections until the cluster is
    -- healed.
    --
    cg.proxy_2_to_1:pause()
    cg.proxy_3_to_1:pause()
    cg.proxy_1_to_2:pause()
    cg.proxy_1_to_3:pause()
    local ok, err = vraft.promote_join_at_wal_release(cg.server1,
                                                      'promote_fiber')
    t.assert_equals(err, nil)
    t.assert(ok)
    local old_term = cg.server1:exec(function()
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
    local new_term = cg.server2:exec(function()
        box.ctl.promote()
        local info = box.info
        t.assert_equals(info.synchro.queue.owner, info.id)
        t.assert_equals(info.election.state, 'leader')
        return info.election.term
    end)
    t.assert_gt(new_term, old_term)
    t.helpers.retrying({timeout = TIMEOUT}, function()
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
    t.helpers.retrying({timeout = TIMEOUT}, function()
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
    t.helpers.retrying({timeout = TIMEOUT}, function()
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
