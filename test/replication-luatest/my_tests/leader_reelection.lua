local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('leader_reelection')

g.before_all(function(cg)

    cg.cluster = cluster:new{}
    cg.nodes = {}
    local box_cfg = {
        election_mode = 'candidate',
        replication_timeout = 0.1, 
        replication = {
            server.build_listen_uri('node1', cg.cluster.id),
            server.build_listen_uri('node2', cg.cluster.id),
            server.build_listen_uri('node3', cg.cluster.id),
        },
    }

    cg.nodes[1] = cg.cluster:build_and_add_server({alias = 'node1', box_cfg = box_cfg})
    cg.nodes[2] = cg.cluster:build_and_add_server({alias = 'node2', box_cfg = box_cfg})
    cg.nodes[3] = cg.cluster:build_and_add_server({alias = 'node3', box_cfg = box_cfg})

    cg.cluster:start()

    for _, node in ipairs(cg.nodes) do
        t.helpers.retrying({timeout = 1, delay = 0.1}, function()
            t.assert_equals(node.net_box.state, 'active', 'Node ' .. node.alias .. ' is connected')
            t.assert_equals(node.net_box:eval('return box.info.status'), 'running', 'Node ' .. node.alias .. ' is running')
        end)
    end
end)

g.after_all(function(cg)
    cg.cluster:drop()
end)

g.test_leader_reassignment = function(cg)

    local initial_leader = cg.cluster:get_leader()
    t.assert(initial_leader ~= nil, 'Изначальный лидер назначен')

    initial_leader:stop()

    local new_leader
    t.helpers.retrying({timeout = 5, delay = 0.2}, function()
        new_leader = cg.cluster:get_leader()
        t.assert(new_leader ~= nil and new_leader ~= initial_leader, 'Назначен новый лидер')
    end)

    t.assert_not_equals(new_leader, initial_leader, 'Назначен новый лидер после остановки предыдущего')
end

