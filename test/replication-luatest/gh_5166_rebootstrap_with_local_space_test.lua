local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all = function(lg)
    lg.master = server:new({
        alias = 'master',
        box_cfg = {checkpoint_count = 1, wal_cleanup_delay = 0}
    })
    lg.master:start()
    lg.master:exec(function()
        box.schema.space.create('l', {is_local = true})
        box.space.l:create_index('pk')
    end)
end

g.after_all = function(lg)
    lg.master:drop()
end

g.test_rebootstrap_with_local_space = function(lg)
    local box_cfg = {
        replication = {
            lg.master.net_box_uri
        }
    }
    local replica = server:new({
        alias = 'replica',
        box_cfg = box_cfg
    })
    replica:start()
    replica:wait_for_vclock_of(lg.master)
    replica:exec(function()
        for i = 1, 10 do
            box.space.l:replace{i}
        end
    end)
    replica:stop()

    -- Restart master to drop gc consumer of replica - then
    -- it will prune xlogs on snapshot
    lg.master:restart()
    lg.master:exec(function()
        box.schema.space.create('s')
        box.space.s:create_index('pk')
        box.space.s:replace{1}
        box.snapshot()
    end)

    -- Check if replica successfully rebootstraps with empty local space
    replica:start()
    replica:wait_for_vclock_of(lg.master)
    replica:exec(function()
        t.assert_equals(box.space.l:select(), {})
    end)
    replica:drop()
end
