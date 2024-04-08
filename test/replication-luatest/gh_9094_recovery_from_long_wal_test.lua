local t = require('luatest')
local cluster = require('luatest.replica_set')

local g = t.group('gh_9094')

g.before_all(function(cg)
    cg.cluster = cluster:new({})
    cg.master = cg.cluster:build_and_add_server{
        alias = 'master',
        box_cfg = {
            replication_timeout = 0.01,
        },
    }
    cg.replica = cg.cluster:build_and_add_server{
        alias = 'replica',
        box_cfg = {
            replication = {
                cg.master.net_box_uri,
            },
            replication_timeout = 0.01,
            read_only = true,
        },
    }
    cg.cluster:start()
end)

g.after_all(function(cg)
    cg.cluster:drop()
end)

g.test_recovery = function(cg)
    cg.master:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
        for i = 1, 1000000 do
            box.space.test:insert{i}
        end
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:stop()

    cg.master:exec(function()
        box.space.test:insert{1000001}
    end)
    cg.master:stop()
    cg.master:start()

    cg.replica:start()
    cg.replica:wait_for_vclock_of(cg.master)

    t.helpers.retrying({}, function()
        cg.replica:assert_follows_upstream(1)
    end)

end
