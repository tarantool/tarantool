local fio = require('fio')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, t.helpers.matrix({
    memtx_use_mvcc_engine = {false, true},
}))

local function assert_master_applier_stopped(cg, replica_id)
    cg.master:exec(function(replica_id)
        t.helpers.retrying({}, function()
            local upstream = box.info.replication[replica_id].upstream
            t.assert_equals(upstream.status, 'stopped')
            t.assert_str_contains(upstream.message, 'Duplicate key exists')
        end)
    end, {replica_id})
end

local function stop_master_applier(cg)
    local old_uuid = cg.replica:get_instance_uuid()
    local old_id = cg.replica:get_instance_id()

    cg.replica:exec(function()
        box.cfg{replication = {}}
    end)
    cg.master:exec(function(old_id)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.replication[old_id].downstream.status,
                            'stopped')
        end)
        box.space._schema:insert{'test'}
    end, {old_id})
    cg.replica:exec(function()
        box.cfg{read_only = false}
        box.space._schema:insert{'test'}
    end)
    assert_master_applier_stopped(cg, old_id)

    return old_uuid, old_id
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
                replication_timeout = 0.1,
                memtx_use_mvcc_engine = cg.params.memtx_use_mvcc_engine,
            },
        })
    end
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
end)

g.after_each(function(cg)
    if t.tarantool.is_debug_build() then
        cg.master:exec(function()
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
            box.error.injection.set('ERRINJ_WAL_WRITE', false)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end)
    end
    cg.replica_set:drop()
end)

g.test_delete_releases_name = function(cg)
    local replica_id = cg.replica:get_instance_id()
    cg.replica:exec(function()
        box.cfg{replication = {}}
    end)
    cg.master:exec(function(replica_id)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.replication[replica_id].downstream.status,
                            'stopped')
        end)
        t.assert_equals(
            box.info.replication[replica_id].upstream.status, 'follow')
        local tuple = box.space._cluster:delete{replica_id}
        t.assert_equals(box.space._cluster:get{replica_id}, nil)
        t.assert_equals(box.info.replication[replica_id], nil)
        t.assert_equals(box.space._cluster:insert(tuple), tuple)
        local info = box.info.replication[replica_id]
        t.assert_equals(info.uuid, tuple[2])
        t.assert_equals(info.name, tuple[3])
        t.assert_equals(info.upstream.status, 'follow')
    end, {replica_id})
end

g.test_uuid_update_rollback_releases_name = function(cg)
    local replica_id = cg.replica:get_instance_id()
    cg.replica:exec(function()
        box.cfg{replication = {}}
    end)
    cg.master:exec(function(replica_id)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.replication[replica_id].downstream.status,
                            'stopped')
        end)
        t.assert_equals(
            box.info.replication[replica_id].upstream.status, 'follow')
        local replacement = box.space._cluster:delete{replica_id}
        local original = {3, require('uuid').str(), 'unconnected'}
        box.space._cluster:insert(original)
        box.begin()
        box.space._cluster:update(3, {{'=', 2, replacement[2]}})
        t.assert_equals(box.info.replication[3].uuid, original[2])
        t.assert_equals(box.info.replication[3].upstream, nil)
        box.rollback()
        t.assert_equals(box.space._cluster:get{3}, original)
        t.assert_equals(box.info.replication[3].uuid, original[2])
        t.assert_equals(box.info.replication[3].name, original[3])
        t.assert_equals(box.info.replication[3].upstream, nil)
        box.space._cluster:insert(replacement)
        t.assert_equals(box.info.replication[replica_id].uuid, replacement[2])
        t.assert_equals(
            box.info.replication[replica_id].upstream.status, 'follow')
    end, {replica_id})
end

g.test_rebootstrap_after_stopped_applier = function(cg)
    local old_uuid, old_id = stop_master_applier(cg)

    cg.replica:drop()
    fio.rmtree(cg.replica.workdir)
    cg.replica:start()

    t.assert_not_equals(cg.replica:get_instance_uuid(), old_uuid)
    t.assert_equals(cg.replica:get_instance_id(), old_id)
    assert_master_applier_stopped(cg, old_id)

    t.helpers.retrying({}, function()
        cg.replica:assert_follows_upstream(cg.master:get_instance_id())
    end)
    cg.replica:wait_for_vclock_of(cg.master)
end

g.test_uuid_update_commit_chain = function(cg)
    local old_uuid, old_id = stop_master_applier(cg)
    cg.master:exec(function(old_uuid, old_id)
        local uuid = require('uuid')
        local message = box.info.replication[old_id].upstream.message
        local final_uuid = uuid.str()
        for _, new_uuid in ipairs({old_uuid, final_uuid}) do
            box.begin()
            box.space._cluster:update(old_id, {{'=', 2, uuid.str()}})
            box.space._cluster:update(old_id, {{'=', 2, new_uuid}})
            box.commit()
            local info = box.info.replication[old_id]
            t.assert_equals(info.uuid, new_uuid)
            t.assert_equals(info.upstream.message, message)
        end

        box.begin()
        box.space._cluster:update(old_id, {{'=', 2, uuid.str()}})
        box.space._cluster:insert{3, final_uuid, 'reappeared'}
        box.space._cluster:update(3, {{'=', 3, 'renamed'}})
        box.commit()
        t.assert_equals(box.info.replication[old_id].upstream, nil)
        t.assert_equals(box.info.replication[3].upstream.message, message)
        t.assert_equals(box.info.replication[3].name, 'renamed')
    end, {old_uuid, old_id})
end

local function prepare_applier_collision(cg)
    local other_uri = server.build_listen_uri('other', cg.replica_set.id)
    local replication = {
        cg.master.net_box_uri, cg.replica.net_box_uri, other_uri,
    }
    local other = cg.replica_set:build_and_add_server({
        alias = 'other',
        box_cfg = {
            instance_name = 'other',
            read_only = true,
            replication = replication,
            replication_timeout = 0.1,
        },
    })
    other:start()
    local other_id = other:get_instance_id()
    cg.master:exec(function(uris)
        box.cfg{replication = uris}
    end, {replication})
    t.helpers.retrying({}, function()
        cg.master:assert_follows_upstream(other_id)
    end)
    other:exec(function()
        box.cfg{replication = {}}
    end)
    local old_uuid, old_id = stop_master_applier(cg)
    -- Keep the destination's applier, but remove its registration.
    local other_tuple = cg.master:exec(function(other_id)
        return box.space._cluster:delete{other_id}:totable()
    end, {other_id})
    return other, old_uuid, old_id, other_tuple
end

g.test_uuid_update_with_existing_applier_rollback = function(cg)
    local _, old_uuid, old_id, other_tuple = prepare_applier_collision(cg)
    cg.master:exec(function(old_uuid, old_id, other_tuple)
        local other_id = other_tuple[1]
        local old_tuple = box.space._cluster:get{old_id}
        local message = box.info.replication[old_id].upstream.message
        box.begin()
        box.space._cluster:update(old_id, {{'=', 2, other_tuple[2]}})
        t.assert_equals(box.info.replication[old_id].upstream.message, message)
        box.rollback()
        t.assert_equals(box.space._cluster:get{old_id}, old_tuple)
        local info = box.info.replication[old_id]
        t.assert_equals(info.uuid, old_uuid)
        t.assert_equals(info.upstream.message, message)

        box.space._cluster:insert(other_tuple)
        t.assert_equals(box.info.replication[other_id].upstream.status,
                        'follow')
        box.space._cluster:delete{other_id}
    end, {old_uuid, old_id, other_tuple})
    t.assert_not(cg.master:grep_log('discarding stopped applier'))
    cg.master:exec(function(old_id, new_uuid)
        box.space._cluster:update(old_id, {{'=', 2, new_uuid}})
        local info = box.info.replication[old_id]
        t.assert_equals(info.uuid, new_uuid)
        t.assert_equals(info.upstream.status, 'follow')
    end, {old_id, other_tuple[2]})
    local line = cg.master:grep_log('discarding stopped applier.*')
    t.assert_str_contains(line, 'Duplicate key exists')
end

g.test_overlapping_uuid_update_rollback = function(cg)
    t.tarantool.skip_if_not_debug()
    local old_uuid, old_id = stop_master_applier(cg)
    cg.master:exec(function(old_uuid, old_id)
        local fiber = require('fiber')
        local new_uuid = require('uuid').str()
        local message = box.info.replication[old_id].upstream.message
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local f1 = fiber.create(function()
            fiber.self():set_joinable(true)
            return pcall(box.space._cluster.update, box.space._cluster,
                         old_id, {{'=', 2, new_uuid}})
        end)
        t.helpers.retrying({}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        local f2 = fiber.create(function()
            fiber.self():set_joinable(true)
            return pcall(box.space._cluster.update, box.space._cluster,
                         old_id, {{'=', 2, old_uuid}})
        end)
        t.assert_equals(box.space._cluster:get{old_id}[2], old_uuid)
        t.assert_equals(box.info.replication[old_id].uuid, old_uuid)
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local joined1, ok1, err1 = f1:join()
        t.assert(joined1)
        t.assert(ok1, tostring(err1))
        t.helpers.retrying({}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        t.assert_equals(box.info.replication[old_id].uuid, new_uuid)
        t.assert_equals(box.info.replication[old_id].upstream.message, message)
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local joined2, ok2, err2 = f2:join()
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
        t.assert(joined2)
        t.assert_not(ok2)
        t.assert_equals(err2.code, box.error.WAL_IO)
        t.assert_equals(box.space._cluster:get{old_id}[2], new_uuid)
        local info = box.info.replication[old_id]
        t.assert_equals(info.uuid, new_uuid)
        t.assert_equals(info.upstream.status, 'stopped')
        t.assert_equals(info.upstream.message, message)
    end, {old_uuid, old_id})
end

g.test_uuid_update_savepoint_commit = function(cg)
    local old_uuid, old_id = stop_master_applier(cg)
    cg.master:exec(function(old_uuid, old_id)
        local uuid = require('uuid')
        local new_uuid = uuid.str()
        local message = box.info.replication[old_id].upstream.message
        box.begin()
        box.space._cluster:update(old_id, {{'=', 2, new_uuid}})
        local savepoint = box.savepoint()
        box.space._cluster:update(old_id, {{'=', 2, uuid.str()}})
        box.space._cluster:insert{3, old_uuid, 'reappeared'}
        box.rollback_to_savepoint(savepoint)
        box.commit()
        t.assert_equals(box.space._cluster:get{old_id}[2], new_uuid)
        t.assert_equals(box.info.replication[old_id].uuid, new_uuid)
        t.assert_equals(box.info.replication[old_id].upstream.message, message)
        t.assert_equals(box.space._cluster:get{3}, nil)
        t.assert_equals(box.info.replication[3], nil)
    end, {old_uuid, old_id})
end

g.test_pending_name_conflict = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.master:exec(function()
        local uuid = require('uuid')
        local fiber = require('fiber')
        box.space._cluster:insert{3, uuid.str(), 'original'}
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local f = fiber.create(function()
            fiber.self():set_joinable(true)
            return pcall(box.space._cluster.update, box.space._cluster,
                         3, {{'=', 3, 'pending'}})
        end)
        t.helpers.retrying({}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        t.assert_equals(box.info.replication[3].name, 'original')
        t.assert_error_covers({code = box.error.INSTANCE_NAME_DUPLICATE},
                             box.space._cluster.insert, box.space._cluster,
                             {4, uuid.str(), 'pending'})
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local joined, ok, err = f:join()
        t.assert(joined)
        t.assert(ok, tostring(err))
        t.assert_equals(box.info.replication[3].name, 'pending')
        box.space._cluster:insert{4, uuid.str(), 'original'}
        t.assert_equals(box.info.replication[4].name, 'original')
    end)
end
