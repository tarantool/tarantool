local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group()

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new{}
    cg.master = cg.replica_set:build_and_add_server({
        box_cfg = {
            election_mode = 'candidate',
        },
        alias = 'master',
    })
    cg.replica = cg.replica_set:build_and_add_server({
        box_cfg = {
            replication = {
                server.build_listen_uri('master', cg.replica_set.id),
            },
            replication_anon = true,
            read_only = true,
        },
        alias = 'replica',
    })
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.test_anon_replica_election_state_is_consistent_after_register = function(cg)
    cg.replica_set:start()
    cg.replica:update_box_cfg({
        replication_anon = false,
    })
    t.helpers.retrying({}, function()
        cg.replica:assert_follows_upstream(cg.master:get_instance_id())
    end)
end
