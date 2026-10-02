local fio = require('fio')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

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
    local old_uuid = tostring(cg.replica:get_instance_uuid())
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
    if t.tarantool.is_debug_build() then
        cg.master:exec(function()
            box.error.injection.set('ERRINJ_WAL_WRITE', false)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end)
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
    assert_master_applier_stopped(cg, old_id)

    t.helpers.retrying({}, function()
        cg.replica:assert_follows_upstream(cg.master:get_instance_id())
    end)
    assert_replicates(cg.master, cg.replica, {1, 'from master'})

    cg.master:exec(function()
        local replication = table.copy(box.cfg.replication)
        box.cfg{replication = {}}
        box.cfg{replication = replication}
    end)
    cg.replica_set:wait_for_fullmesh()
    cg.replica:exec(function()
        box.cfg{read_only = false}
    end)
    assert_replicates(cg.replica, cg.master, {2, 'from replica'})
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

g.test_uuid_update_rollback = function(cg)
    local old_uuid, old_id = stop_master_applier(cg)

    cg.master:exec(function(old_uuid, old_id)
        local uuid = require('uuid')
        local tuple = box.space._cluster:get{old_id}
        local message = box.info.replication[old_id].upstream.message
        box.begin()
        box.space._cluster:update(old_id, {{'=', 2, uuid.str()}})
        local savepoint = box.savepoint()
        local intermediate_uuid = box.info.replication[old_id].uuid
        box.space._cluster:update(old_id, {{'=', 2, uuid.str()}})
        box.rollback_to_savepoint(savepoint)
        t.assert_equals(box.info.replication[old_id].uuid, intermediate_uuid)
        box.rollback()
        t.assert_equals(box.space._cluster:get{old_id}, tuple)
        local info = box.info.replication[old_id]
        t.assert_equals(info.uuid, old_uuid)
        t.assert_equals(info.name, 'replica')
        t.assert_equals(info.upstream.status, 'stopped')
        t.assert_equals(info.upstream.message, message)
    end, {old_uuid, old_id})
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
        box.commit()
        t.assert_equals(box.info.replication[old_id].upstream, nil)
        t.assert_equals(box.info.replication[3].upstream.message, message)
    end, {old_uuid, old_id})
end

local function test_uuid_update_wal_failure_template(cg, action)
    t.tarantool.skip_if_not_debug()
    local old_uuid, old_id = stop_master_applier(cg)

    cg.master:exec(function(old_uuid, old_id, reconfigure)
        local fiber = require('fiber')
        local new_uuid = require('uuid').str()
        local tuple = box.space._cluster:get{old_id}
        local replication = table.copy(box.cfg.replication)
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local f = fiber.create(function()
            fiber.self():set_joinable(true)
            return pcall(box.space._cluster.update, box.space._cluster,
                         old_id, {{'=', 2, new_uuid}})
        end)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.replication[old_id].uuid, new_uuid)
        end)
        box.cfg{replication = {}}
        if reconfigure == 'replace' then
            box.cfg{replication = replication}
        end
        box.error.injection.set('ERRINJ_WAL_WRITE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local joined, ok, err = f:join()
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
        t.assert(joined)
        t.assert_not(ok)
        t.assert_equals(err.code, box.error.WAL_IO)
        t.assert_equals(box.space._cluster:get{old_id}, tuple)
        local info = box.info.replication[old_id]
        t.assert_equals(info.uuid, old_uuid)
        t.assert_equals(info.name, 'replica')
        if reconfigure == 'remove' then
            t.assert_equals(info.upstream, nil)
        else
            t.assert_not_equals(info.upstream, nil)
        end
    end, {old_uuid, old_id, action})
end

g.test_uuid_update_wal_failure_remove = function(cg)
    test_uuid_update_wal_failure_template(cg, 'remove')
end

g.test_uuid_update_wal_failure_replace = function(cg)
    test_uuid_update_wal_failure_template(cg, 'replace')
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
        t.assert_equals(box.info.replication[old_id].upstream.status, 'follow')
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
    t.assert(cg.master:grep_log('discarding stopped applier'))
end

g.test_existing_stopped_applier_rollback = function(cg)
    local other, _, old_id, tuple = prepare_applier_collision(cg)
    other:exec(function()
        box.cfg{read_only = false}
        box.space._schema:insert{'test'}
    end)
    cg.master:exec(function(old_id, tuple)
        t.helpers.retrying({}, function()
            box.begin()
            box.space._cluster:update(old_id, {{'=', 2, tuple[2]}})
            local upstream = box.info.replication[old_id].upstream
            box.rollback()
            t.assert_equals(upstream.status, 'stopped')
        end)
        -- Rollback must not delete a pre-existing unregistered applier.
        box.space._cluster:insert(tuple)
        local upstream = box.info.replication[tuple[1]].upstream
        t.assert_equals(upstream.status, 'stopped')
        t.assert_str_contains(upstream.message, 'Duplicate key exists')
    end, {old_id, tuple})
    assert_master_applier_stopped(cg, old_id)
end

g.test_rebootstrap_with_existing_applier = function(cg)
    t.tarantool.skip_if_not_debug()
    local other, _, old_id = prepare_applier_collision(cg)
    cg.replica:stop()
    other:drop()
    fio.rmtree(other.workdir)
    other.box_cfg.instance_name = 'replica'
    other.box_cfg.replication = {cg.master.net_box_uri, other.net_box_uri}
    cg.master:exec(function(old_id)
        rawset(_G, 'delay_registration', function(old, new)
            if old ~= nil and new ~= nil and old[1] == old_id and
               old[2] ~= new[2] then
                box.error.injection.set('ERRINJ_WAL_DELAY', true)
            end
        end)
        box.space._cluster:on_replace(_G.delay_registration)
    end, {old_id})
    other:start({wait_until_ready = false})
    cg.master:exec(function(old_id, other_uri)
        -- Commit must keep the applier that connected during registration.
        t.helpers.retrying({}, function()
            local upstream = box.info.replication[old_id].upstream
            t.assert(upstream ~= nil)
            t.assert_str_contains(upstream.peer, other_uri)
        end)
        box.space._cluster:on_replace(nil, _G.delay_registration)
        _G.delay_registration = nil
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end, {old_id, other.net_box_uri})
    other:wait_until_ready()
    t.assert_equals(other:get_instance_id(), old_id)
    t.helpers.retrying({}, function()
        cg.master:assert_follows_upstream(old_id)
    end)
    t.assert(cg.master:grep_log('discarding stopped applier'))
    other:exec(function()
        box.cfg{read_only = false}
    end)
    assert_replicates(other, cg.master, {1, 'existing applier'})
end

g.test_rebootstrap_wal_failure = function(cg)
    t.tarantool.skip_if_not_debug()
    local old_uuid, old_id = stop_master_applier(cg)
    cg.replica:drop()
    fio.rmtree(cg.replica.workdir)
    cg.master:exec(function(old_id)
        rawset(_G, 'fail_registration', function(old, new)
            if old ~= nil and new ~= nil and old[1] == old_id and
               old[2] ~= new[2] then
                rawset(_G, 'registration_attempted', true)
                box.error.injection.set('ERRINJ_WAL_WRITE', true)
            end
        end)
        box.space._cluster:on_replace(_G.fail_registration)
    end, {old_id})
    cg.replica:start({wait_until_ready = false})
    cg.master:exec(function(old_uuid, old_id)
        t.helpers.retrying({}, function()
            t.assert(rawget(_G, 'registration_attempted'))
            t.assert_equals(box.info.replication[old_id].uuid, old_uuid)
        end)
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
        box.space._cluster:on_replace(nil, _G.fail_registration)
        _G.fail_registration = nil
        _G.registration_attempted = nil
        t.assert_equals(box.space._cluster:get{old_id}[2], old_uuid)
        t.assert_equals(box.info.replication[old_id].name, 'replica')
    end, {old_uuid, old_id})
    assert_master_applier_stopped(cg, old_id)

    cg.replica:drop()
    fio.rmtree(cg.replica.workdir)
    cg.replica:start()
    t.assert_equals(cg.replica:get_instance_id(), old_id)
    assert_master_applier_stopped(cg, old_id)
end
