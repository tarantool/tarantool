local fio = require('fio')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')
local uuid = require('uuid')

local g = t.group()
local function stop_master_applier(cg)
    local old_uuid = tostring(cg.replica:get_instance_uuid())
    local old_id = cg.replica:get_instance_id()

    cg.replica:exec(function()
        box.cfg{replication = {}}
    end)
    cg.master:exec(function()
        box.space._schema:insert{'test'}
    end)
    cg.replica:exec(function()
        box.cfg{read_only = false}
        box.space._schema:insert{'test'}
    end)
    cg.master:exec(function(replica_id)
        t.helpers.retrying({}, function()
            local upstream = box.info.replication[replica_id].upstream
            t.assert_equals(upstream.status, 'stopped')
            t.assert_str_contains(upstream.message, 'Duplicate key exists')
        end)
    end, {old_id})

    return old_uuid, old_id
end

g.before_each(function(cg)
    cg.replica_set = replica_set:new({})
    local replication = {
        server.build_listen_uri('master', cg.replica_set.id),
        server.build_listen_uri('replica', cg.replica_set.id),
    }
    cg.master = cg.replica_set:build_and_add_server({
        alias = 'master',
        box_cfg = {
            instance_name = 'master',
            replication = {replication[1], replication[2]},
        },
    })
    cg.replica = cg.replica_set:build_and_add_server({
        alias = 'replica',
        box_cfg = {
            instance_name = 'replica',
            read_only = true,
            replication = {replication[1], replication[2]},
        },
    })
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function()
        box.schema.space.create('test'):create_index('pk')
    end)
    cg.replica:wait_for_vclock_of(cg.master)
end)

g.after_each(function(cg)
    if cg.new_replica ~= nil then
        cg.new_replica:drop()
    end
    cg.replica_set:drop()
end)

g.test_rebootstrap_after_stopped_applier = function(cg)
    local old_uuid, old_id = stop_master_applier(cg)

    cg.replica:drop()
    fio.rmtree(cg.replica.workdir)
    cg.replica:start()

    t.assert_not_equals(tostring(cg.replica:get_instance_uuid()), old_uuid)
    t.assert_equals(cg.replica:get_instance_id(), old_id)

    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function()
        box.space.test:insert{1, 'from master'}
    end)

    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        t.assert_equals(box.space.test:get{1}:totable(),
                        {1, 'from master'})
        box.cfg{read_only = false}
        box.space.test:insert{2, 'from replica'}
    end)
    cg.master:wait_for_vclock_of(cg.replica)
    cg.master:exec(function()
        t.assert_equals(box.space.test:get{2}:totable(),
                        {2, 'from replica'})
    end)
end

g.test_rebootstrap_after_failed_validation = function(cg)
    local old_uuid, old_id = stop_master_applier(cg)
    local u2_uuid = uuid.str()
    t.assert_not_equals(u2_uuid, old_uuid)

    cg.replica:exec(function()
        box.cfg{read_only = true}
    end)

    local box_cfg = table.copy(cg.replica.box_cfg)
    box_cfg.instance_uuid = u2_uuid
    box_cfg.listen = server.build_listen_uri('new_replica',
                                             cg.replica_set.id)
    box_cfg.replication = {
        server.build_listen_uri('master', cg.replica_set.id),
        server.build_listen_uri('replica', cg.replica_set.id),
        box_cfg.listen,
    }
    cg.new_replica = server:new({
        alias = 'new_replica',
        box_cfg = box_cfg,
    })
    cg.new_replica:start({wait_until_ready = false})

    cg.master:exec(function(replica_id)
        t.helpers.retrying({}, function()
            local upstream = box.info.replication[replica_id].upstream
            t.assert_equals(upstream.status, 'stopped')
            t.assert_str_contains(upstream.message,
                                  'Instance UUID mismatch')
        end)
    end, {old_id})
    t.helpers.retrying({}, function()
        t.assert(cg.new_replica:grep_log(
            'Duplicate replica name replica, already occupied', 1024))
    end)
    cg.new_replica:drop()

    cg.replica:drop()
    fio.rmtree(cg.replica.workdir)
    cg.replica.box_cfg.instance_uuid = u2_uuid
    cg.replica:start()

    local new_uuid = tostring(cg.replica:get_instance_uuid())
    t.assert_equals(new_uuid, u2_uuid)
    t.assert_equals(cg.replica:get_instance_id(), old_id)
    cg.replica_set:wait_for_fullmesh()
end
