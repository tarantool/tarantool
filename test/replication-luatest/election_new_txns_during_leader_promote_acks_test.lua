local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('cover-box-wait-limbo-acked')
local wait_timeout = 120

local function wait_pair_sync(server1, server2)
    t.helpers.retrying({timeout = wait_timeout}, function()
        server1:wait_for_vclock_of(server2)
        server2:wait_for_vclock_of(server1)
        server1:assert_follows_upstream(server2:get_instance_id())
        server2:assert_follows_upstream(server1:get_instance_id())
    end)
end

local function wait_full_sync(cg, names)
    for _, name1 in pairs(names) do
        for _, name2 in pairs(names) do
            if name1 ~= name2 then
                wait_pair_sync(cg[name1], cg[name2])
            end
        end
    end
end

--
-- The test covers the following scenario:
-- 1. A node had a transaction from the old leader.
-- 2. The node starts elections.
-- 3. It receives a new transaction from the old leader, while the elections are
--   already ongoing.
-- 4. It receives votes and wins the elections.
-- 5. It waits for a quorum on both txns.
-- 6. It writes PROMOTE and becomes a true leader.
--
-- The expected behaviour is that the node will wait for both transactions to
-- gain the quorum. Not like it would only collect quorum for whatever
-- transaction was the last at the moment of elections start.
--

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.cluster = cluster:new({})
    cg.test_make_uri = function(name)
        return server.build_listen_uri(name, cg.cluster.id)
    end
    --
    -- The test tries to reproduce the case in a realistic way with as few
    -- error injections as possible, without manipulating the quorum in an
    -- unreal way, and without using election_mode='off', which is broken
    -- anyway.
    --
    -- For that the test needs many nodes, used for certain roles declared in
    -- their names. Although technically all the nodes here are identical in
    -- their setup.
    --
    local all_names = {'node1', 'node2', 'voter3', 'voter4', 'stash5'}
    local box_cfg = {
        replication = {},
        replication_synchro_quorum = 3,
        replication_synchro_timeout = 100000,
        replication_timeout = 0.1,
        election_fencing_mode='off',
        election_timeout = 1000,
    }
    for _, name in pairs(all_names) do
        table.insert(box_cfg.replication, cg.test_make_uri(name))
    end
    for _, name in pairs(all_names) do
        if name == 'node1' then
            box_cfg.election_mode = 'manual'
        else
            box_cfg.election_mode = 'voter'
        end
        cg[name] = cg.cluster:build_and_add_server({
            alias = name,
            box_cfg = box_cfg
        })
    end
    cg.cluster:start()
    --
    -- Restart replication to apply the new timeout. Just setting the timeout
    -- isn't enough, because some already waiting calls in relay/applier still
    -- can be waiting on the old timeout. Need to restart all these places.
    --
    for _, name in pairs(all_names) do
        cg[name]:update_box_cfg({
            election_mode = 'manual',
            -- Since 3.5 it is 1000. But it only work combined with the
            -- replication_reconnect_timeout option, which doesn't exist in 3.3
            -- and below. The timeout 10 is unlikely to ever fail, but it will
            -- sometimes stall the test for 10 secs.
            replication_timeout = 10,
            replication = {},
        })
    end
    for _, name in pairs(all_names) do
        cg[name]:update_box_cfg({replication = box_cfg.replication})
    end
    wait_full_sync(cg, all_names)
    cg.node1:exec(function()
        box.ctl.promote()
        box.schema.space.create('test', {is_sync = true})
        box.space.test:create_index('pk')
    end)
    wait_full_sync(cg, all_names)
    cg.test_fullmesh_replication = box_cfg.replication
    cg.test_all_names = all_names
end)

g.after_each(function(cg)
    cg.cluster:drop()
end)

g.test_new_synchronous_transactions_appeared_while_wait_quorum = function(cg)
    --
    -- Split the cluster:
    --
    -- {node1, stash5}
    -- {node2, voter3, voter4}
    --
    local cfg_replication_15 = {
        cg.test_make_uri('node1'),
        cg.test_make_uri('stash5'),
    }
    cg.node1:update_box_cfg({replication = cfg_replication_15})
    cg.stash5:update_box_cfg({replication = cfg_replication_15})
    local cfg_replication_234 = {
        cg.test_make_uri('node2'),
        cg.test_make_uri('voter3'),
        cg.test_make_uri('voter4'),
    }
    cg.node2:update_box_cfg({replication = cfg_replication_234})
    cg.voter3:update_box_cfg({replication = cfg_replication_234})
    cg.voter4:update_box_cfg({replication = cfg_replication_234})
    --
    -- Node1 makes a txn and stashes it on the stash5 node.
    --
    cg.node1:exec(function()
        local fiber = require('fiber')
        rawset(_G, 'f_txn1', fiber.new(function()
            box.space.test:replace{1}
        end))
    end)
    cg.stash5:exec(function(timeout)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
        end)
    end, {wait_timeout})
    --
    -- Node1 and stash5 are fully isolated alone.
    --
    cg.node1:update_box_cfg({replication = {}})
    cg.stash5:update_box_cfg({replication = {}})
    --
    -- Node1 makes a second txn.
    --
    cg.node1:exec(function()
        local fiber = require('fiber')
        rawset(_G, 'f_txn2', fiber.new(function()
            box.space.test:replace{2}
        end))
    end)
    --
    -- The state now is:
    --
    -- {node1:  ?txn1, ?txn2}
    -- {node2, voter3, voter4}
    -- {stash5: ?txn1}
    --
    -- Node2 must start new elections. But the test must not allow it to win
    -- yet. It must only let it broadcast the vote request. Then node2 would be
    -- controllably stuck in ongoing elections.
    --
    -- For that make the voters stop WAL writes when they receive a vote
    -- request.
    --
    local block_on_vote_f = function(timeout)
        local fiber = require('fiber')
        rawset(_G, 'f_block_until_vote', fiber.new(function()
            fiber.self():set_joinable(true)
            repeat
                fiber.testcancel()
                box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
                box.error.injection.set('ERRINJ_WAL_DELAY', false)
                t.helpers.retrying({timeout = timeout}, function()
                    if pcall(fiber.testcancel) then
                        t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
                    end
                end)
            until box.info.election.vote ~= 0
        end))
    end
    cg.voter3:exec(block_on_vote_f, {wait_timeout})
    cg.voter4:exec(block_on_vote_f, {wait_timeout})
    --
    -- Node2 starts promotion.
    --
    cg.node2:exec(function()
        local fiber = require('fiber')
        rawset(_G, 'f_promote', fiber.new(function()
            fiber.self():set_joinable(true)
            box.ctl.promote()
        end))
    end)
    --
    -- The voters receive the vote request.
    --
    local finish_vote_f = function(timeout)
        t.assert((_G.f_block_until_vote:join(timeout)))
        t.assert_not_equals(box.info.election.vote, 0)
        -- And must not download whatever will be happening with node2.
        box.cfg{replication = {}}
    end
    cg.voter3:exec(finish_vote_f, {wait_timeout})
    cg.voter4:exec(finish_vote_f, {wait_timeout})
    --
    -- The state now:
    --
    -- {node1: ?txn1, ?txn2}
    -- {node2: candidate}
    -- {voter3, voter4: prepared to vote for node2}
    -- {stash5: ?txn1}
    --
    -- While the voters are "thinking", the candidate node2 receives txn1 made
    -- by the old leader. Stash5 node (which only has txn1) will provide it.
    --
    local cfg_replication_25 = {
        cg.test_make_uri('node2'),
        cg.test_make_uri('stash5'),
    }
    cg.node2:update_box_cfg({replication = cfg_replication_25})
    --
    -- The voters finish their votes. Not responding to node2 yet. There is no
    -- replication between them.
    --
    local finish_wal_work_f = function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end
    cg.voter3:exec(finish_wal_work_f)
    cg.voter4:exec(finish_wal_work_f)
    --
    -- Node2, still candidate, receives txn1 from stash5. It is received after
    -- node2 has already started elections, but before the elections are won.
    --
    cg.stash5:update_box_cfg({replication = cfg_replication_25})
    cg.node2:exec(function(timeout)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
        end)
    end, {wait_timeout})
    --
    -- The voters send their votes to node2, which in turn wins the elections.
    -- But it can't be promoted yet, because it needs to gain a quorum on the
    -- txn1. This won't happen, because the voter nodes download nothing from
    -- node2 and won't ack txn1. Only node1 downloads from the voters, not vice
    -- versa.
    --
    cg.node2:update_box_cfg({replication = cfg_replication_234})
    cg.node2:exec(function(timeout)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.info.election.state, 'leader')
        end)
        -- Promotion isn't done yet, although the elections are won.
        t.assert_lt(box.info.synchro.queue.term, box.info.election.term)
    end, {wait_timeout})
    --
    -- While waiting, node2 receives the txn2 from node1. This txn was received
    -- after winning the elections, but before writing PROMOTE.
    --
    cg.node2:update_box_cfg({replication = cg.test_fullmesh_replication})
    cg.node2:exec(function(timeout)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.info.synchro.queue.len, 2)
        end)
        t.assert_equals(box.info.election.state, 'leader')
        t.assert_lt(box.info.synchro.queue.term, box.info.election.term)
    end, {wait_timeout})
    --
    -- All replication is restored.
    --
    for _, name in pairs(cg.test_all_names) do
        cg[name]:update_box_cfg({replication = cg.test_fullmesh_replication})
    end
    --
    -- The node2 must wait for the last txn (txn2) to gain the quorum and write
    -- PROMOTE including this txn. Even though it was received after winning the
    -- elections.
    --
    cg.node2:exec(function(timeout)
        t.assert((_G.f_promote:join(timeout)))
        t.assert_equals(box.info.election.state, 'leader')
        t.assert_equals(box.info.synchro.queue.term, box.info.election.term)
        t.assert_equals(box.space.test:select(), {{1}, {2}})
    end, {wait_timeout})
end
