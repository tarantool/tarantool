local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('pre-vote')

local REPLICATION_TIMEOUT = 0.1
local DEATH_TIMEOUT = 5 * REPLICATION_TIMEOUT

--
-- gh-6654: pre-vote.
-- The nodes shouldn't start elections when the elections would be either
-- disruptive (there's an active leader somewhere) or pointless (the node
-- doesn't have a quorum of peers).
--
g.before_all(function()
    g.cluster = cluster:new({})
    g.cfg = {
        replication = {
            server.build_listen_uri('node1'),
            server.build_listen_uri('node2'),
            server.build_listen_uri('node3'),
        },
        election_mode = 'candidate',
        replication_timeout = REPLICATION_TIMEOUT,
        election_timeout = DEATH_TIMEOUT,
    }
    g.node1 = g.cluster:build_and_add_server({alias = 'node1', box_cfg = g.cfg})
    g.node2 = g.cluster:build_and_add_server({alias = 'node2', box_cfg = g.cfg})
    g.node3 = g.cluster:build_and_add_server({alias = 'node3', box_cfg = g.cfg})
    g.cluster:start()
end)

g.before_each(function()
    g.leader = g.cluster:get_leader()
    t.assert(g.leader ~= nil, 'Cluster elected a leader')
    g.follower1 = g.node1 ~= g.leader and g.node1 or g.node2
    g.follower2 = g.node3 ~= g.leader and g.node3 or g.node2
    g.cluster:wait_for_fullmesh()
end)

local function get_election_term()
    return box.info.election.term
end

local function assert_leader_idle(val)
    require('luatest').assert(box.info.election.leader_idle > val)
end

--
-- Test that the node doesn't start elections in the following conditions:
--  - the cluster has a leader
--  - the node doesn't see the leader directly
--  - the node has a quorum of connections to remote peers (excluding leader)
--
g.test_no_direct_connection = function(g)
    local term = g.follower1:exec(get_election_term)
    g.follower1:exec(function(cfg) box.cfg(cfg) end,
        {{
            replication = {
               server.build_listen_uri(g.follower1.alias),
               server.build_listen_uri(g.follower2.alias),
            }
        }})
    t.helpers.retrying({}, function()
        g.follower1:exec(assert_leader_idle, {DEATH_TIMEOUT})
    end)
    t.assert_equals(g.follower1:exec(get_election_term), term,
                    'No elections when leader is seen through '..
                    'some proxy node')
    g.follower1:exec(function(cfg) box.cfg(cfg) end,
        {{replication = g.cfg.replication}})
    t.assert_equals(g.follower1:exec(get_election_term), term,
                    'No elections after leader connection regain')
end

--
-- Test that the node doesn't start elections in the following conditions:
--  - the cluster has a leader
--  - the node doesn't see the leader directly or indirectly
--  - the node doesn't have a quorum of peers
--
g.test_no_quorum = function(g)
    g.follower1:exec(function() box.cfg{replication = ''} end)
    local term = g.follower1:exec(get_election_term)
    t.helpers.retrying({}, function()
        g.follower1:exec(assert_leader_idle, {DEATH_TIMEOUT})
    end)
    t.assert_equals(g.follower1:exec(get_election_term), term,
                    'No elections when no leader and no quorum')
    g.follower1:exec(function(cfg) box.cfg(cfg) end,
        {{
            replication = {
               server.build_listen_uri(g.follower1.alias),
               server.build_listen_uri(g.follower2.alias),
            }
        }})
    t.assert_equals(g.follower1:exec(get_election_term), term,
                    'No elections after quorum regain')
    g.follower1:exec(function(cfg) box.cfg(cfg) end,
        {{replication = g.cfg.replication}})
    t.assert_equals(g.follower1:exec(get_election_term), term,
                    'No elections after leader reconnect')
end

--
-- Test that the node doesn't enter infinite election loop in box.ctl.promote(),
-- when it lacks a quorum of peers.
--
g.test_promote_no_quorum = function(g)
    g.follower1:exec(function() box.cfg{replication = ''} end)
    local term = g.follower1:exec(get_election_term)
    t.assert_error_msg_content_equals(
        'Not enough peers connected to start '..
        'elections: 1 out of minimal required 2',
        g.follower1.exec, g.follower1, function() box.ctl.promote() end)
    t.assert(g.follower1:exec(get_election_term) > term,
             'Elections are started once')
end

g.after_test('test_promote_no_quorum', function(g)
    g.follower1:exec(function(cfg) box.cfg(cfg) end,
        {{replication = g.cfg.replication}})
end)

g.after_all(function()
    g.cluster:drop()
end)
