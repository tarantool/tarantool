local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('gh-8931')
--
-- gh-8931:
-- There is the leader field in the given informational API, but it is easier
-- to a human to understand human-readable names. We have instance names
-- since #5029. Let's show them in this API.
--
g.before_all(function(cg)
    cg.cluster = cluster:new({})
    local cfg = {
        replication = {
            server.build_listen_uri('node1', cg.cluster.id),
            server.build_listen_uri('node2', cg.cluster.id)
        },
        election_mode = 'candidate'
    }
    cg.node1 = cg.cluster:build_and_add_server({alias = 'node1', box_cfg = cfg})
    cg.node2 = cg.cluster:build_and_add_server({alias = 'node2', box_cfg = cfg})
    cg.cluster:start()
end)

g.before_test('test_leader_has_no_name', function(cg)
    cg.node1:wait_until_election_leader_found()
    cg.node2:wait_until_election_leader_found()
end)

g.test_leader_has_no_name = function(cg)
    cg.node1:exec(function()
        local function is_null(val)
            if val == nil and val then
                return true
            end
            return false
        end
        t.assert(is_null(box.info.election.leader_name))
    end)
end

g.before_test('test_leader_has_a_name', function(cg)
    cg.node1:exec(function()
        box.cfg{instance_name = 'node1'}
    end)
    cg.node2:exec(function()
        box.cfg{instance_name = 'node2'}
    end)
end)

g.test_leader_has_a_name = function(cg)
    cg.cluster:get_leader():exec(function()
        t.assert_equals(box.info.election.leader_name, box.info.name)
    end)
end

g.before_test('test_no_leader', function(cg)
    cg.node1:exec(function()
        box.cfg{election_mode = 'off'}
    end)
    cg.node2:exec(function()
        box.cfg{election_mode = 'off'}
    end)
    cg.node1:wait_for_election_state('follower')
    cg.node2:wait_for_election_state('follower')
end)

g.test_no_leader = function(cg)
    cg.node1:exec(function()
        local function is_nil(val)
            if val == nil and not val then
                return true
            end
            return false
        end
        t.assert(is_nil(box.info.election.leader_name))
    end)
end

g.after_all(function(cg)
    cg.cluster:drop()
end)
