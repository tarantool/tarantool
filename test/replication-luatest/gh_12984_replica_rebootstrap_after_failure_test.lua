local fio = require('fio')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

local function assert_replicates(source, destination, tuple)
    source:exec(function(tuple)
        box.space.test:insert(tuple)
    end, {tuple})
    destination:wait_for_vclock_of(source)
    destination:exec(function(tuple)
        t.assert_equals(box.space.test:get{tuple[1]}:totable(), tuple)
    end, {tuple})
end

g.before_each(function(cg)
    cg.replica_set = replica_set:new({})
    local replication = {
        server.build_listen_uri('master', cg.replica_set.id),
        server.build_listen_uri('replica', cg.replica_set.id),
    }
    for _, name in ipairs({'master', 'replica'}) do
        cg[name] = cg.replica_set:build_and_add_server({
            alias = name,
            box_cfg = {
                instance_name = name,
                read_only = name == 'replica',
                replication = replication,
            },
        })
    end
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function()
        box.schema.space.create('test'):create_index('pk')
    end)
    cg.replica:wait_for_vclock_of(cg.master)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.test_uuid_update_without_applier_rollback = function(cg)
    cg.master:exec(function()
        local uuid = require('uuid')
        local original = uuid.str()
        box.space._cluster:insert{3, original, 'unconnected'}
        box.begin()
        box.space._cluster:update(3, {{'=', 2, uuid.str()}})
        box.rollback()
        t.assert_equals(box.space._cluster:get{3}[2], original)
        t.assert_equals(box.info.replication[3].uuid, original)
    end)
end

g.test_rebootstrap_with_non_stopped_applier = function(cg)
    local old_uuid = tostring(cg.replica:get_instance_uuid())
    local old_id = cg.replica:get_instance_id()

    cg.replica:drop()
    fio.rmtree(cg.replica.workdir)
    cg.replica:start()

    t.assert_not_equals(tostring(cg.replica:get_instance_uuid()), old_uuid)
    t.assert_equals(cg.replica:get_instance_id(), old_id)
    cg.replica_set:wait_for_fullmesh()

    assert_replicates(cg.master, cg.replica, {1, 'from master'})
    cg.replica:exec(function()
        box.cfg{read_only = false}
    end)
    assert_replicates(cg.replica, cg.master, {2, 'from replica'})
end
