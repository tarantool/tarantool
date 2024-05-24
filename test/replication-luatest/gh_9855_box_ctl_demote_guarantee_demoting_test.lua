local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group()

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication = {
            server.build_listen_uri('server1', cg.replica_set.id),
            server.build_listen_uri('server2', cg.replica_set.id),
        },
        election_mode = 'candidate',
        replication_timeout = 0.1,
        election_timeout = 0.5,
    }
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = box_cfg,
    }
    box_cfg.election_mode = 'voter'
    cg.server2 = cg.replica_set:build_and_add_server{
        alias = 'server2',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

-- Test that the demoted leader can still win in subsequent elections.
local function test_subsequent_elections(demoted_leader, current_leader)
    current_leader:exec(function()
        box.ctl.demote()
    end)
    demoted_leader:wait_for_election_leader();
    demoted_leader:exec(function()
        t.assert_equals(box.info.election.vote, box.info.election.leader)
    end)
    current_leader:exec(function()
        t.assert_equals(box.info.election.vote, box.info.election.leader)
    end)
end

-- Test that the demoted leader cannot get elected after resigning himself
-- through `box.ctl.demote`.
g.test_demote_guarantee = function(cg)
    cg.server1:wait_for_election_leader()
    cg.server2:update_box_cfg({election_mode = 'candidate'})
    cg.server1:exec(function()
        box.ctl.demote()
    end)
    cg.server2:wait_for_election_leader();
    cg.server1:exec(function()
        t.assert_equals(box.info.election.vote, box.info.election.leader)
    end)
    test_subsequent_elections(cg.server1, cg.server2)
end

-- Test that the demoted leader waits for leader death timeout after resigning
-- himself through `box.ctl.demote` and starts subsequent elections.
g.test_demoted_leader_election_in_subsequent_elections = function(cg)
    cg.server1:wait_for_election_leader()
    local demoted_term = cg.server1:get_election_term()
    cg.server1:exec(function()
        box.ctl.demote()
    end)
    cg.server1:wait_for_election_leader()
    cg.server1:exec(function(demoted_term)
        t.assert_equals(box.info.election.term, demoted_term + 1)
        t.assert_equals(box.info.election.vote, box.info.election.leader)
    end, {demoted_term})
end
