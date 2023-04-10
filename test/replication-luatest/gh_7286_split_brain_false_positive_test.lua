local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('gh-7286')

-- gh-7286: false positive ER_SPLIT_BRAIN error after box.ctl.demote() with
-- disabled elections.
g.before_all(function(cg)
    cg.cluster = cluster:new{}

    cg.box_cfg = {
        -- Just in case it's turned on by default sometime.
        election_mode = 'off',
        replication_timeout = 0.1,
        replication = {
            server.build_listen_uri('node1', cg.cluster.id),
            server.build_listen_uri('node2', cg.cluster.id),
        },
    }
    cg.node1 = cg.cluster:build_and_add_server{
        alias = 'node1',
        box_cfg = cg.box_cfg,
    }
    cg.node2 = cg.cluster:build_and_add_server{
        alias = 'node2',
        box_cfg = cg.box_cfg,
    }
    cg.cluster:start()
end)

g.after_all(function(cg)
    cg.cluster:drop()
end)

g.test_false_positive_split_brain = function(cg)
    cg.node1:exec(function()
        box.ctl.promote()
        box.ctl.demote()
    end)
    cg.node2:wait_for_vclock_of(cg.node1)
    cg.node2:exec(function()
        box.space._schema:replace{'smth'}
    end)
    cg.node1:wait_for_vclock_of(cg.node2)
    cg.node1:assert_follows_upstream(cg.node2:get_instance_id())
end
