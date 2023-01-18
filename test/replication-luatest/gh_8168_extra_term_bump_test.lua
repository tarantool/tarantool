local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('gh-8168')

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication = {
            server.build_listen_uri('server1'),
            server.build_listen_uri('server2'),
        },
        election_mode = 'manual',
        replication_timeout = 0.1,
    }
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = box_cfg,
    }
    box_cfg.election_timeout = 1e-9
    box_cfg.read_only = true
    cg.server2 = cg.replica_set:build_and_add_server{
        alias = 'server2',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.test_double_term_bump_on_promote = function(cg)
    cg.server1:wait_for_election_leader()
    local term = cg.server1:get_election_term()
    local ok, _ = cg.server2:exec(function()
        return pcall(box.ctl.promote)
    end)
    t.assert(ok, 'Promote succeeded')
    t.assert_equals(cg.server2:get_election_term(), term + 1,
                    'Promote bumped term once')
    t.assert_equals(cg.server2:exec(function()
        return box.info.election.state
    end), 'leader', 'Server 2 is the leader')
end
