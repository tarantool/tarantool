local t = require('luatest')
local cluster = require('luatest.replica_set')

local g = t.group('gh-7584')

g.before_all(function(cg)
    cg.cluster = cluster:new{}
    cg.master = cg.cluster:build_and_add_server{
        alias = 'master',
        box_cfg = {
            checkpoint_count = 1,
        },
    }
    cg.replica = cg.cluster:build_and_add_server{
        alias = 'replica',
        box_cfg = {
            replication = {
                cg.master.net_box_uri,
            },
        },
    }
    cg.cluster:start()
    cg.master:exec(function()
        box.schema.space.create('loc', {is_local = true})
        box.space.loc:create_index('pk')
        box.schema.space.create('glob')
        box.space.glob:create_index('pk')
    end)
end)

g.after_all(function(cg)
    cg.cluster:drop()
end)

g.test_xlog_gap = function(cg)
    t.helpers.retrying({}, cg.replica.assert_follows_upstream, cg.replica, 1)
    cg.replica:stop()
    cg.master:exec(function()
        for _ = 1, 2 do
            box.space.loc:replace{1}
            box.space.glob:replace{1}
            box.snapshot()
        end
    end)
    cg.replica:start()
    t.helpers.retrying({}, cg.replica.assert_follows_upstream, cg.replica, 1)
end
