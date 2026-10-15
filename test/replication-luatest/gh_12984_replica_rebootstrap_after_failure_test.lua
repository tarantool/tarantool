local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, t.helpers.matrix({
    memtx_use_mvcc_engine = {false, true},
}))

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
