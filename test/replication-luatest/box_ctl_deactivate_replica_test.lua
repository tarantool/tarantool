local t = require('luatest')
local server = require('luatest.server')
local uuid = require('uuid')

local g = t.group()

g.before_each(function()
    g.master = server:new{box_cfg = {replication_timeout = 0.1}}
    g.master:start()
end)

g.after_each(function()
    if g.replica ~= nil then
        g.replica:drop()
        g.replica = nil
    end
    g.master:drop()
end)

g.test_invalid_usage = function()
    g.master:exec(function()
        local fake_uuid = require('uuid').str()
        t.assert_error_msg_content_equals(
            "Cannot deactivate replica: does not exist",
            box.ctl.deactivate_replica, fake_uuid)

        t.assert_error_msg_content_equals(
            "Cannot deactivate replica: deactivation of " ..
            "self is not allowed",
            box.ctl.deactivate_replica, box.info.uuid)
    end)
end

g.test_usual_replica = function()
    g.master:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        s:replace{1}
    end)
    local replica_uuid = uuid.str()
    local box_cfg = table.deepcopy(g.master.box_cfg)
    box_cfg.replication = {g.master.net_box_uri}
    box_cfg.instance_uuid = replica_uuid
    g.replica = server:new({
        alias = 'replica',
        box_cfg = box_cfg,
    })
    g.replica:start()
    g.replica:wait_for_vclock_of(g.master)
    g.master:exec(function(replica_uuid)
        t.assert_error_msg_content_equals(
            "Cannot deactivate replica: it is connected",
            box.ctl.deactivate_replica, replica_uuid)
    end, {replica_uuid})
    g.replica:stop()
    g.master:exec(function(replica_uuid)
        -- Replica must be registered
        t.assert_not_equals(box.space._cluster:get(2), nil)
        -- WAL GC consumer must be created
        t.assert_not_equals(box.info.gc().consumers, {})
        t.assert_not_equals(box.space._gc_consumers:select{}, {})

        box.ctl.deactivate_replica(replica_uuid)

        -- Replica still must be registered
        t.assert_not_equals(box.space._cluster:get(2), nil)
        -- However, WAL GC consumers must be deleted
        t.assert_equals(box.info.gc().consumers, {})
        t.assert_equals(box.space._gc_consumers:select{}, {})
    end, {replica_uuid})
end

g.test_anon_replica = function()
    g.master:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        s:replace{1}
    end)
    local replica_uuid = uuid.str()
    local box_cfg = table.deepcopy(g.master.box_cfg)
    box_cfg.replication = {g.master.net_box_uri}
    box_cfg.instance_uuid = replica_uuid
    box_cfg.replication_anon = true
    box_cfg.read_only = true
    g.replica = server:new({
        alias = 'replica',
        box_cfg = box_cfg,
    })
    g.replica:start()
    g.replica:wait_for_vclock_of(g.master)
    g.master:exec(function(replica_uuid)
        t.assert_error_msg_content_equals(
            "Cannot deactivate replica: it is connected",
            box.ctl.deactivate_replica, replica_uuid)
    end, {replica_uuid})
    g.replica:stop()
    g.master:exec(function(replica_uuid)
        -- Anonymous replica must be created
        t.assert_equals(box.info.replication_anon.count, 1)
        -- WAL GC consumer must be created
        t.assert_not_equals(box.info.gc().consumers, {})
        t.assert_not_equals(box.space._gc_consumers:select{}, {})

        box.ctl.deactivate_replica(replica_uuid)

        -- Anonymous replica must be removed
        t.assert_equals(box.info.replication_anon.count, 0)
        -- WAL GC consumers must be deleted
        t.assert_equals(box.info.gc().consumers, {})
        t.assert_equals(box.space._gc_consumers:select{}, {})
    end, {replica_uuid})
end

g.test_wal_error = function()
    t.tarantool.skip_if_not_debug()
    g.master:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        s:replace{1}
    end)
    local replica_uuid = uuid.str()
    local box_cfg = table.deepcopy(g.master.box_cfg)
    box_cfg.replication = {g.master.net_box_uri}
    box_cfg.instance_uuid = replica_uuid
    g.replica = server:new({
        alias = 'replica',
        box_cfg = box_cfg,
    })
    g.replica:start()
    g.replica:wait_for_vclock_of(g.master)
    g.replica:stop()

    g.master:exec(function(replica_uuid)
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        t.assert_error_msg_content_equals("Failed to write to disk",
            box.ctl.deactivate_replica, replica_uuid)
        box.error.injection.set('ERRINJ_WAL_WRITE', false)

        -- Check if WAL GC consumers are still alive
        t.assert_not_equals(box.info.gc().consumers, {})
        t.assert_not_equals(box.space._gc_consumers:select{}, {})
    end, {replica_uuid})
    g.replica:start()
    g.master:exec(function()
        for i = 1, 10 do
            box.space.test:replace{i}
        end
    end)
    g.replica:wait_for_vclock_of(g.master)
end
