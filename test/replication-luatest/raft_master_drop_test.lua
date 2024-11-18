local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local fio = require('fio')

local g = t.group('my-raft')

g.before_all(function(cg)
    cg.cluster = cluster:new{}
    local box_cfg = {
        election_mode = 'candidate',
        replication_timeout = 0.1,
        replication_synchro_quorum = 1,
        replication = {
            server.build_listen_uri('master', cg.cluster.id),
            server.build_listen_uri('slave1', cg.cluster.id),
            server.build_listen_uri('slave2', cg.cluster.id),

        },
    }
    cg.master = cg.cluster:build_and_add_server{
        alias = 'master',
        box_cfg = box_cfg,
    }
    box_cfg.election_mode='voter'
    cg.slave1  = cg.cluster:build_and_add_server{
        alias = 'slave1',
        box_cfg = box_cfg,
    }

    cg.slave2 = cg.cluster:build_and_add_server{
        alias = 'slave2',
        box_cfg = box_cfg,
    }

    cg.cluster:start()
    cg.slave1:wait_until_election_leader_found()
    cg.slave2:wait_until_election_leader_found()
end)

g.after_all(function(cg)
    cg.cluster:drop()
end)



g.test_my_raft = function(cg)
    cg.slave1:exec(function()
        box.cfg{election_mode='candidate'}
    end)
    cg.master:drop()
    cg.slave1:wait_for_election_state('leader')
    cg.slave2:wait_until_election_leader_found()
    cg.slave1:exec(function() t.assert_equals(box.info.election.state, 'leader',"slave1 is leader") end)
    cg.slave2:exec(function() t.assert(box.info.election.state ~= 'leader', 'slave2 is still slave') end)

end
