local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group()

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new{}
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = {
            replication_timeout = 0.1,
        },
    }
    cg.replica = cg.replica_set:build_and_add_server{
        alias = 'replica',
        box_cfg = {
            replication = {
                server.build_listen_uri('master', cg.replica_set.id),
            },
            replication_timeout = 0.1,
            wal_queue_max_size = 1,
            -- We want to check that correct wal_queue_max_size is in effect
            -- during sync, so set huge sync timeout to make sure sync doesn't
            -- end too fast and an old code path (setting size after sync) isn't
            -- tested.
            replication_sync_timeout = 300,
        },
    }
    cg.replica_set:start()
    cg.master:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
    end)
    cg.replica:wait_for_vclock_of(cg.master)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

local run_before_cfg = [[
    rawset(_G, 'wal_write_count', 0)
    box.error.injection.set('ERRINJ_WAL_DELAY' , true)
    wal_write_count = box.error.injection.get('ERRINJ_WAL_WRITE_COUNT')
]]

-- gh-10013: wal_queue_max_size wasn't respected during initial box.cfg() call,
-- and the replica used the default value (16 Mb) during sync. Test that this is
-- fixed: introduce a WAL delay before initial box.cfg() on replica, write some
-- data on master to be synced with, make sure replica respects queue max size.
g.test_wal_queue_max_size_apply_on_initial_sync = function(cg)
    cg.replica:stop()
    cg.master:exec(function()
        for i = 1,10 do
            box.space.test:insert{i}
        end
    end)
    cg.replica.env['TARANTOOL_RUN_BEFORE_BOX_CFG'] = run_before_cfg
    cg.replica:start({wait_until_ready=false})
    t.helpers.retrying({}, cg.replica.connect_net_box, cg.replica)
    cg.replica:exec(function()
        t.helpers.retrying({}, function()
            t.assert_equals(box.error.injection.get('ERRINJ_WAL_WRITE_COUNT'),
                            _G.wal_write_count + 1)
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.helpers.retrying({}, function()
            t.assert_equals(box.error.injection.get('ERRINJ_WAL_WRITE_COUNT'),
                            _G.wal_write_count + 10)
        end)
    end)
    cg.replica:wait_for_vclock_of(cg.master)
end
