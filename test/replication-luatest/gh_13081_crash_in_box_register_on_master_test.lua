local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group()

g.before_all(function(cg)
    cg.replica_set = replica_set:new({})
    cg.master_uri = server.build_listen_uri('master', cg.replica_set.id)
    cg.replica_uri = server.build_listen_uri('replica', cg.replica_set.id)
    cg.master = cg.replica_set:build_and_add_server({alias = 'master'})
    cg.replica = cg.replica_set:build_and_add_server({
        alias = 'replica',
        box_cfg = {
            replication = {cg.master_uri},
            replication_timeout = 0.1,
            read_only = true,
        },
    })
    cg.replica_set:start()
    cg.replica:wait_for_vclock_of(cg.master)
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

g.test_no_crash_on_instance_name_register_via_self = function(cg)
    cg.replica:exec(function(uri)
        -- Switch replication to self-only. The self-applier becomes the
        -- only candidate for join master since the master applier no longer
        -- exists in the replicaset hash.
        box.cfg{replication = {uri}}
        -- Attempting to set instance_name must produce ER_CANNOT_REGISTER,
        -- not a crash. The name and cfg must remain unchanged.
        t.assert_error_msg_contains('Couldn\'t find an instance to ' ..
            'register this replica on', box.cfg,
            {instance_name = 'replica_name'})
        t.assert_equals(box.info.name, nil)
        t.assert_equals(box.cfg.instance_name, nil)
    end, {cg.replica_uri})
    t.assert(cg.replica.process:is_alive())

    -- Restore the master upstream and verify that registration works.
    cg.replica:exec(function(uri)
        box.cfg{instance_name = 'replica_name', replication = {uri}}
        t.assert_equals(box.info.name, 'replica_name')
    end, {cg.master_uri})
end
