local replica_set = require('luatest.replica_set')
local t = require('luatest')

local g = t.group(nil, t.helpers.matrix({
    memtx_use_mvcc_engine = {false, true},
}))

g.before_each(function(cg)
    cg.replica_set = replica_set:new({})
    cg.master = cg.replica_set:build_and_add_server({
        alias = 'master',
        box_cfg = {
            instance_uuid = 'cccccccc-cccc-cccc-cccc-cccccccccccc',
            memtx_use_mvcc_engine = cg.params.memtx_use_mvcc_engine,
        },
    })
    cg.replica_set:start()
end)

g.after_each(function(cg)
    if t.tarantool.is_debug_build() then
        cg.master:exec(function()
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
            box.error.injection.set('ERRINJ_REPLICA_JOIN_DELAY', false)
        end)
    end
    cg.replica_set:drop()
end)

g.test_savepoint_releases_reservation = function(cg)
    cg.master:exec(function()
        local uuid = require('uuid')
        local first = {2, uuid.str()}
        local second = {31, uuid.str()}
        box.begin()
        box.space._cluster:insert(first)
        local savepoint = box.savepoint()
        box.space._cluster:insert(second)
        box.rollback_to_savepoint(savepoint)
        t.assert_equals(box.space._cluster:insert(second), second)
        box.commit()
        t.assert_equals(box.info.replication[2].uuid, first[2])
        t.assert_equals(box.info.replication[31].uuid, second[2])
        box.space._cluster:delete{31}
        t.assert_equals(box.space._cluster:insert(second), second)
    end)
end

g.test_concurrent_registration = function(cg)
    t.tarantool.skip_if_not_debug()
    local replicas = {}
    for i = 1, 2 do
        local replica = cg.replica_set:build_and_add_server({
            alias = 'replica' .. i,
            box_cfg = {
                replication = cg.master.net_box_uri,
                replication_anon = true,
                read_only = true,
                memtx_use_mvcc_engine = cg.params.memtx_use_mvcc_engine,
            },
        })
        replica:start()
        replica:wait_for_vclock_of(cg.master)
        replicas[i] = replica
    end
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_REPLICA_JOIN_DELAY', true)
    end)
    local uuids = {}
    for i, replica in ipairs(replicas) do
        uuids[i] = replica:get_instance_uuid()
        replica:exec(function()
            local fiber = require('fiber')
            rawset(_G, 'register_fiber', fiber.create(function()
                fiber.self():set_joinable(true)
                box.cfg{replication_anon = false}
            end))
        end)
        local msg = ('registering replica ' .. uuids[i]):gsub('%-', '%%-')
        t.helpers.retrying({}, function()
            t.assert(cg.master:grep_log(msg))
        end)
    end
    local ids = cg.master:exec(function(uuids)
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        box.error.injection.set('ERRINJ_REPLICA_JOIN_DELAY', false)
        local ids = {}
        t.helpers.retrying({}, function()
            for i, uuid in ipairs(uuids) do
                local tuple = box.space._cluster.index.uuid:get{uuid}
                t.assert(tuple)
                ids[i] = tuple[1]
            end
        end)
        t.assert_not_equals(ids[1], ids[2])
        for _, id in ipairs(ids) do
            t.assert_equals(box.info.replication[id], nil)
        end
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        return ids
    end, {uuids})
    for i, replica in ipairs(replicas) do
        replica:exec(function(id)
            local ok, err = _G.register_fiber:join()
            t.assert(ok, tostring(err))
            t.assert_equals(box.info.id, id)
        end, {ids[i]})
        cg.master:exec(function(uuid, id)
            t.assert_equals(box.info.replication[id].uuid, uuid)
            t.assert_equals(box.space._cluster:get{id}[2], uuid)
        end, {uuids[i], ids[i]})
    end
end

g.test_uuid_lookup_ignores_case = function(cg)
    cg.master:exec(function()
        local id = box.info.id
        local uuid = box.info.uuid
        box.space._cluster:update(id, {{'=', 2, uuid:upper()}})
        t.assert_equals(box.info.id, id)
        t.assert_equals(box.info.uuid, uuid)
        box.cfg{instance_name = 'master'}
        t.assert_equals(box.info.id, id)
        t.assert_equals(box.info.name, 'master')
        t.assert_equals(box.space._cluster:count(), 1)
        t.assert_equals(
            box.space._cluster:get{id}, {id, uuid:upper(), 'master'})
    end)
end

g.test_uuid_update_checks_decoded_uuid = function(cg)
    cg.master:exec(function()
        local cluster = box.space._cluster
        local first = {2, 'bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb', 'first'}
        local second = {3, 'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa', 'second'}
        cluster:insert(first)
        cluster:insert(second)
        t.assert_error_msg_contains(
            'UUID update when the new UUID is already registered',
            cluster.update, cluster, 2, {{'=', 2, second[2]:upper()}})
        t.assert_equals(cluster:get{2}, first)
        t.assert_equals(cluster:get{3}, second)
        t.assert_equals(box.info.replication[2].uuid, first[2])
        t.assert_equals(box.info.replication[3].uuid, second[2])
    end)
end
