local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('synchro-filter-enable-by-version')

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    cg.box_cfg = {
        replication = {
            server.build_listen_uri('server1', cg.replica_set.id),
            server.build_listen_uri('server2', cg.replica_set.id),
        },
        replication_timeout = 0.1,
    }
    for i = 1,2 do
        cg['server' .. i] = cg.replica_set:build_and_add_server{
            alias = 'server' .. i,
            box_cfg = cg.box_cfg,
        }
    end
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

-- Check that split-brain detection does not work with schema version <=
-- 2.10.1, and is re-enabled back after a schema upgrade.
g.test_filter_enable_disable = function(cg)
    cg.replica_set:start()
    cg.server1:exec(function()
        box.ctl.wait_rw()
        box.schema.downgrade('2.10.1')
        t.assert_equals(box.space._schema:get{'version'},
                        {'version', 2, 10, 1})
    end)
    cg.server2:wait_for_vclock_of(cg.server1)

    cg.server1:update_box_cfg({replication = ""})
    cg.server2:update_box_cfg({replication = ""})

    cg.server1:exec(function()
        box.ctl.promote()
    end)
    cg.server2:exec(function()
        box.ctl.promote()
    end)

    cg.server1:update_box_cfg(cg.box_cfg)
    cg.server2:update_box_cfg(cg.box_cfg)
    cg.server1:wait_for_vclock_of(cg.server2)
    cg.server2:wait_for_vclock_of(cg.server1)
    cg.server1:assert_follows_upstream(cg.server2:get_instance_id())
    cg.server2:assert_follows_upstream(cg.server1:get_instance_id())

    cg.server1:update_box_cfg({replication = ""})
    cg.server2:update_box_cfg({replication = ""})

    for i = 1,2 do
        cg['server' .. i]:exec(function()
            box.ctl.promote()
            box.schema.upgrade()
        end)
    end

    t.helpers.retrying({}, function()
        for i = 1,2 do
            cg['server' .. i]:update_box_cfg(cg.box_cfg)
            cg['server' .. i]:exec(function(id)
                t.assert_equals(box.info.replication[id].upstream.status,
                                'stopped')
                t.assert_str_contains(box.info.replication[id].upstream.message,
                                      'Split-Brain discovered')
            end, {cg['server' .. 3 - i]:get_instance_id()})
        end
    end)
end
