local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.master = server:new({
        alias = "master",
        box_cfg = {
            checkpoint_interval = 0,
            wal_max_size = 1.0e9,
            replication_timeout = 1.0,
        }
    })
    cg.master:start()
    cg.replica = server:new({
        alias = "replica",
        box_cfg = {
            replication = cg.master.net_box_uri,
            checkpoint_interval = 0,
            wal_max_size = 1e9,
            replication_timeout = 1.0,
        }
    })
    cg.replica:start()
    cg.master:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        local data = string.rep("a", 4096)
        for i = 1, 128 do
            box.space.test:insert({i, data})
        end
        box.error.injection.set('ERRINJ_XLOG_READ_ROW_DELAY', 0.1)
    end)
    cg.master:exec(function()
        local tweaks = require('internal.tweaks')
        tweaks.xlog_row_bytes_per_yield = 1e9
    end)
    cg.master:wait_for_downstream_to(cg.replica)
    cg.replica:update_box_cfg{replication = ""}
end)

g.after_all(function(cg)
    cg.replica:update_box_cfg{replication = cg.master.net_box_uri}
    cg.master:drop()
    cg.replica:drop()
end)

g.test_row_bytes_per_yield_tweak = function(cg)
    cg.master:exec(function()
        local tweaks = require('internal.tweaks')
        tweaks.xlog_row_bytes_per_yield = 16384
        box.space.test:replace{500, 1}
    end)
    cg.replica:update_box_cfg{replication = cg.master.net_box_uri}
    cg.master:wait_for_downstream_to(cg.replica)
end
