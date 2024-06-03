local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g_one_member_cluster = t.group('one_member_cluster')
local g_three_member_cluster = t.group('three_member_cluster')

g_one_member_cluster.before_all(function(cg)
    cg.master = server:new{alias = 'master'}
    cg.master:start()
    -- Make `_cluster` space synchronous.
    cg.master:exec(function()
        box.ctl.promote()
        box.space._cluster:alter{is_sync = true}
    end)
end)

-- Test that synchronous insertion into 1-member cluster works properly.
g_one_member_cluster.test_insertion = function(cg)
    cg.replica = server:new{alias = 'replica', box_cfg = {
        replication = cg.master.net_box_uri,
    }}
    cg.replica:start()
    cg.master:wait_for_downstream_to(cg.replica)
    cg.replica:exec(function()
        t.assert_not_equals(box.space._cluster:get{box.info.id}, nil)
    end)
end

g_one_member_cluster.after_all(function(cg)
    cg.master:drop()
    if cg.replica ~= nil then
        cg.replica:drop()
    end
end)

g_three_member_cluster.before_all(function(cg)
    cg.replica_set = replica_set:new{}
    cg.master = cg.replica_set:build_and_add_server{alias = 'master'}
    cg.master:start()
    cg.replica_to_be_disabled =
        cg.replica_set:build_and_add_server{alias = 'to_be_disabled',
                                            box_cfg = {
        replication = {
            cg.master.net_box_uri,
            server.build_listen_uri('replica', cg.replica_set.id),
        },
    }}
    cg.replica = cg.replica_set:build_and_add_server{alias = 'replica',
                                                     box_cfg = {
        replication = {
            cg.master.net_box_uri,
            server.build_listen_uri('to_be_disabled', cg.replica_set.id),
        },
    }}
    cg.replica_set:start()

    -- Make `_cluster` space synchronous.
    cg.master:exec(function()
        box.ctl.promote()
        box.space._cluster:alter{is_sync = true}
    end)

    cg.master:wait_for_downstream_to(cg.replica_to_be_disabled)
    cg.master:wait_for_downstream_to(cg.replica)
end)

-- Test that synchronous insertion into 3-member cluster with 1 disabled node
-- works properly.
g_three_member_cluster.test_insertion = function(cg)
    cg.replica_to_be_disabled:exec(function()
        box.cfg{replication = ''}
    end)
    cg.replica_to_be_added =
        cg.replica_set:build_and_add_server{alias = 'to_be_added',
                                            box_cfg = {
        replication = {
            cg.master.net_box_uri,
            server.build_listen_uri('to_be_disabled', cg.replica_set.id),
            server.build_listen_uri('replica', cg.replica_set.id),
        },
        replication_sync_timeout = 0,
    }}
    cg.replica_to_be_added:start()
    cg.master:wait_for_downstream_to(cg.replica)
    cg.master:wait_for_downstream_to(cg.replica_to_be_added)
    cg.replica_to_be_added:exec(function()
        t.assert_not_equals(box.space._cluster:get{box.info.id}, nil)
    end)
end

g_three_member_cluster.after_all(function(cg)
    cg.replica_set:drop()
end)
