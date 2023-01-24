local fio = require('fio')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

local function wait_for_death(instance)
    t.helpers.retrying({}, function()
        assert(not instance.process:is_alive())
    end)
    -- Nullify already dead process or server:drop() fails.
    instance.process = nil
end

g.before_all = function(lg)
    lg.replica_set = replica_set:new({})
    local box_cfg = {
        replication = {
            server.build_listen_uri('master', lg.replica_set.id),
            server.build_listen_uri('replica', lg.replica_set.id),
        },
        replication_timeout = 0.1,
        instance_name = 'master-name',
    }
    lg.master = lg.replica_set:build_and_add_server({
        alias = 'master',
        box_cfg = box_cfg,
    })
    box_cfg.read_only = true
    box_cfg.instance_name = 'replica-name'
    lg.replica = lg.replica_set:build_and_add_server({
        alias = 'replica',
        box_cfg = box_cfg,
    })
    lg.replica_set:start()
end

g.after_all = function(lg)
    lg.replica_set:drop()
end

g.test_local_errors = function(lg)
    lg.master:exec(function()
        local msg = 'expected a valid name'
        t.assert_error_msg_contains(msg, box.cfg, {instance_name = '123'})
        t.assert_error_msg_contains(msg, box.cfg, {instance_name = '-abc'})
        t.assert_error_msg_contains(msg, box.cfg, {instance_name = 'a~b'})

        msg = 'does not support'
        t.assert_error_msg_contains(msg, box.cfg, {instance_name = 'test'})

        msg = 'type does not match'
        local _cluster = box.space._cluster
        t.assert_error_msg_contains(msg, _cluster.update, _cluster, {1},
                                    {{'=', 'name', 'bad name'}})
        t.assert_error_msg_contains(msg, _cluster.update, _cluster, {1},
                                    {{'=', 'name', 100}})
    end)
    lg.replica:exec(function()
        local msg = "Couldn't find an instance to register this replica on"
        local replication = box.cfg.replication
        box.cfg{replication = {}}
        t.assert_error_msg_contains(msg, box.cfg, {instance_name = 'test'})
        box.cfg{replication = replication}
    end)
end

g.test_instance_name_basic = function(lg)
    local check_name_f = function()
        local info = box.info
        local _cluster = box.space._cluster
        t.assert_equals(info.name, _cluster:get{info.id}.name)
        t.assert_equals(info.name, box.cfg.instance_name)
        t.assert_equals(info.replication[1].name, 'master-name')
        t.assert_equals(info.replication[2].name, 'replica-name')
    end
    lg.master:exec(check_name_f)
    lg.replica:exec(check_name_f)
end

g.test_instance_rename = function(lg)
    --
    -- Drop the name.
    --
    lg.master:exec(function()
        rawset(_G, 'last_event', {})
        rawset(_G, 'watcher', box.watch('box.id', function(_, event)
            _G.last_event = event
        end))
        box.cfg{
            force_recovery = true,
            instance_name = box.NULL,
        }
        box.space._cluster:update({1}, {{'=', 'name', box.NULL}})
        local info = box.info
        t.assert_equals(info.name, nil)
        t.assert_equals(info.replication[1].name, nil)
        t.assert_equals(info.replication[2].name, 'replica-name')
        t.assert_equals(_G.last_event.instance_name, nil)
    end)
    lg.replica:wait_for_vclock_of(lg.master)
    lg.replica:exec(function()
        box.cfg{
            force_recovery = true,
            instance_name = box.NULL,
        }
        local info = box.info
        t.assert_equals(info.name, 'replica-name')
        t.assert_equals(info.replication[1].name, nil)
        t.assert_equals(info.replication[2].name, 'replica-name')
    end)
    lg.master:exec(function()
        box.space._cluster:update({2}, {{'=', 'name', box.NULL}})
        local info = box.info
        t.assert_equals(info.name, nil)
        t.assert_equals(info.replication[1].name, nil)
        t.assert_equals(info.replication[2].name, nil)
    end)
    lg.replica:wait_for_vclock_of(lg.master)
    lg.replica:exec(function()
        local info = box.info
        t.assert_equals(info.name, nil)
        t.assert_equals(info.replication[1].name, nil)
        t.assert_equals(info.replication[2].name, nil)
    end)
    --
    -- Replace with the same nil name.
    --
    lg.master:exec(function()
        local _cluster = box.space._cluster
        _cluster:replace(_cluster:get{1})
        t.assert_equals(_G.last_event.instance_name, nil)
        t.assert_equals(box.info.name, nil)
    end)
    --
    -- Change nil -> not nil.
    --
    lg.master:exec(function()
        box.cfg{
            force_recovery = false,
            instance_name = 'master-name'
        }
        t.assert_equals(box.info.name, 'master-name')
        t.assert_equals(_G.last_event.instance_name, 'master-name')
    end)
    lg.replica:exec(function()
        box.cfg{
            force_recovery = false,
            instance_name = 'replica-name'
        }
        t.assert_equals(box.info.name, 'replica-name')
    end)
    --
    -- Change not nil -> same not nil.
    --
    lg.master:exec(function()
        local _cluster = box.space._cluster
        _cluster:replace(_cluster:get{1})
        t.assert_equals(box.info.name, 'master-name')
    end)
    --
    -- Change not nil -> new not nil.
    --
    lg.master:exec(function()
        -- The name is converted to a proper form automatically.
        box.cfg{
            force_recovery = true,
            instance_name = 'master-NAME-new'
        }
        t.assert_equals(box.cfg.instance_name, 'master-name-new')
        t.assert_equals(box.info.name, 'master-name-new')
        t.assert_equals(_G.last_event.instance_name, 'master-name-new')
    end)
    lg.replica:exec(function()
        box.cfg{
            force_recovery = true,
            instance_name = 'replica-NAME-new'
        }
        t.assert_equals(box.cfg.instance_name, 'replica-name-new')
        t.assert_equals(box.info.name, 'replica-name-new')
    end)
    --
    -- Cleanup.
    --
    lg.master:exec(function()
        _G.watcher:unregister()
        _G.watcher = nil
        _G.last_event = nil
        box.cfg{instance_name = 'master-name'}
        box.cfg{force_recovery = false}
    end)
    lg.replica:exec(function()
        box.cfg{instance_name = 'replica-name'}
        box.cfg{force_recovery = false}
    end)
    lg.replica:wait_for_vclock_of(lg.master)
    lg.master:wait_for_vclock_of(lg.replica)
end

g.test_instance_name_transactional = function(lg)
    lg.master:exec(function()
        t.assert_equals(box.info.name, 'master-name')
        box.cfg{force_recovery = true}
        box.begin()
        box.space._cluster:update({1}, {{'=', 'name', 'new-name'}})
        t.assert_equals(box.info.name, 'new-name')
        box.rollback()
        t.assert_equals(box.info.name, 'master-name')
        box.cfg{force_recovery = false}
    end)
end

g.test_instance_name_bootstrap_mismatch = function(lg)
    --
    -- New replica has no instance name, but the master gave it. Works fine.
    --
    lg.master:exec(function()
        rawset(_G, 'test_trigger', function(_, new)
            if new then
                return new:update({{'=', 3, 'other-name'}})
            end
            return nil
        end)
        box.space._cluster:before_replace(_G.test_trigger)
    end)
    local box_cfg = table.copy(lg.replica.box_cfg)
    box_cfg.instance_name = nil
    local new_replica = server:new({
        alias = 'new_replica',
        box_cfg = box_cfg,
    })
    new_replica:start()
    new_replica:exec(function()
        t.assert_equals(box.info.name, 'other-name')
    end)
    new_replica:drop()
    --
    -- New replica has instance name, but the master gave a different one.
    --
    local function cluster_delete_by_name(name)
        local _cluster = box.space._cluster
        for _, t in _cluster:pairs() do
            if t.name == name then
                _cluster:delete{t.id}
                return
            end
        end
    end
    lg.master:exec(cluster_delete_by_name, {'other-name'})
    box_cfg.instance_name = 'new-replica-name'
    box_cfg.bootstrap_strategy = 'legacy'
    new_replica = server:new({
        alias = 'new_replica',
        box_cfg = box_cfg,
    })
    new_replica:start({wait_until_ready = false})
    local logfile = fio.pathjoin(new_replica.workdir,
                                 new_replica.alias .. '.log')
    wait_for_death(new_replica)
    t.assert(new_replica:grep_log(
        'Instance name mismatch: expected new%-replica%-name, got other%-name',
        1024, {filename = logfile}))
    new_replica:drop()
    --
    -- New replica has instance name, but the master didn't assign it.
    --
    lg.master:exec(cluster_delete_by_name, {'other-name'})
    lg.master:exec(function()
        local _cluster = box.space._cluster
        _cluster:before_replace(nil, _G.test_trigger)
        _G.test_trigger = function(_, new)
            if new then
                return new:update({{'=', 3, box.NULL}})
            end
            return nil
        end
        _cluster:before_replace(_G.test_trigger)
    end)
    new_replica = server:new({
        alias = 'new_replica',
        box_cfg = box_cfg,
    })
    new_replica:start({wait_until_ready = false})
    logfile = fio.pathjoin(new_replica.workdir, new_replica.alias .. '.log')
    wait_for_death(new_replica)
    t.assert(new_replica:grep_log(
        'Instance name mismatch: expected new%-replica%-name, got <no%-name>',
        1024, {filename = logfile}))
    new_replica:drop()
    --
    -- Cleanup.
    --
    lg.master:exec(function(replica_id)
        local own_id = box.info.id
        local _cluster = box.space._cluster
        _cluster:before_replace(nil, _G.test_trigger)
        _G.test_trigger = nil
        for _, v in _cluster:pairs() do
            local id = v.id
            if id ~= own_id and id ~= replica_id then
                _cluster:delete{id}
            end
        end
    end, {lg.replica:get_instance_id()})
end

g.test_instance_name_recovery_mismatch = function(lg)
    --
    -- Has name in WAL, other name in cfg.
    --
    local box_cfg = table.copy(lg.replica.box_cfg)
    box_cfg.instance_name = 'new-name'
    -- Force recovery won't help.
    box_cfg.force_recovery = true
    lg.replica:restart({
        box_cfg = box_cfg,
    }, {wait_until_ready = false})
    local logfile = fio.pathjoin(lg.replica.workdir,
                                 lg.replica.alias .. '.log')
    wait_for_death(lg.replica)
    t.assert(lg.replica:grep_log(
        'Instance name mismatch: expected new%-name, got replica%-name', 1024,
        {filename = logfile}))
    --
    -- Has name in WAL, no name in cfg. Then the replica uses the saved name, no
    -- conflict.
    --
    box_cfg.instance_name = nil
    -- Don't need force_recovery for this.
    box_cfg.force_recovery = nil
    lg.replica:restart({
        box_cfg = box_cfg,
    })
    lg.replica:exec(function()
        t.assert_equals(box.cfg.instance_name, nil)
        t.assert_equals(box.info.name, 'replica-name')
    end)
    --
    -- No name in WAL, has name in cfg.
    --
    lg.replica:exec(function()
        box.cfg{force_recovery = true}
    end)
    lg.master:exec(function()
        box.space._cluster:update({2}, {{'=', 'name', box.NULL}})
    end)
    lg.replica:wait_for_vclock_of(lg.master)
    lg.replica:exec(function()
        t.assert_equals(box.info.name, nil)
    end)
    box_cfg.instance_name = 'new-name'
    -- Force recovery won't help.
    box_cfg.force_recovery = true
    lg.replica:restart({
        box_cfg = box_cfg,
    }, {wait_until_ready = false})
    logfile = fio.pathjoin(lg.replica.workdir, lg.replica.alias .. '.log')
    wait_for_death(lg.replica)
    t.assert(lg.replica:grep_log(
        'Instance name mismatch: expected new%-name, got <no%-name>', 1024,
        {filename = logfile}))
    box_cfg.instance_name = nil
    box_cfg.force_recovery = nil
    lg.replica:restart({
        box_cfg = box_cfg,
    })
    --
    -- Master can't change the name on recovery either.
    --
    -- Has name in WAL, other name in cfg.
    --
    box_cfg = table.copy(lg.master.box_cfg)
    box_cfg.instance_name = 'new-name'
    -- Force recovery won't help.
    box_cfg.force_recovery = true
    lg.master:restart({
        box_cfg = box_cfg,
    }, {wait_until_ready = false})
    logfile = fio.pathjoin(lg.master.workdir, lg.master.alias .. '.log')
    wait_for_death(lg.master)
    t.assert(lg.master:grep_log(
        'Instance name mismatch: expected new%-name, got master%-name', 1024,
        {filename = logfile}))
    --
    -- No name in WAL, has name in cfg.
    --
    box_cfg.instance_name = nil
    -- Don't need force_recovery for this.
    box_cfg.force_recovery = nil
    lg.master:restart({
        box_cfg = box_cfg,
    })
    lg.master:exec(function()
        box.cfg{force_recovery = true}
        box.space._cluster:update({1}, {{'=', 'name', box.NULL}})
        t.assert_equals(box.info.name, nil)
    end)
    box_cfg.instance_name = 'new-name'
    -- Force recovery won't help.
    box_cfg.force_recovery = true
    lg.master:restart({
        box_cfg = box_cfg,
    }, {wait_until_ready = false})
    logfile = fio.pathjoin(lg.master.workdir, lg.master.alias .. '.log')
    wait_for_death(lg.master)
    t.assert(lg.master:grep_log(
        'Instance name mismatch: expected new%-name, got <no%-name>', 1024,
        {filename = logfile}))
    box_cfg.instance_name = nil
    box_cfg.force_recovery = nil
    lg.master:restart({
        box_cfg = box_cfg,
    })
    --
    -- Cleanup.
    --
    lg.replica:exec(function()
        box.cfg{force_recovery = true}
    end)
    lg.master:exec(function()
        box.cfg{force_recovery = true}
        local _cluster = box.space._cluster
        _cluster:update({1}, {{'=', 'name', 'master-name'}})
        _cluster:update({2}, {{'=', 'name', 'replica-name'}})
        box.cfg{force_recovery = false}
    end)
    lg.replica:wait_for_vclock_of(lg.master)
    lg.replica:exec(function()
        box.cfg{force_recovery = false}
    end)
    lg.replica.box_cfg.instance_name = 'replica-name'
    lg.master.box_cfg.instance_name = 'master-name'
end

g.test_instance_name_change_batch = function(lg)
    lg.replica:exec(function()
        box.cfg{
            force_recovery = true,
            replication = {},
        }
    end)
    lg.master:exec(function()
        t.assert_equals(box.info.replication[2].name, 'replica-name')
        local _cluster = box.space._cluster
        for _ = 1, 3 do
            _cluster:update({2}, {{'=', 'name', 'replica-name-new'}})
            _cluster:update({2}, {{'=', 'name', 'replica-name'}})
        end
        t.assert_equals(box.info.replication[2].name, 'replica-name')
    end)
    lg.replica:exec(function(replication)
        box.cfg{
            force_recovery = true,
            replication = replication,
        }
    end, {lg.replica.box_cfg.replication})
    lg.replica:wait_for_vclock_of(lg.master)
    lg.replica:exec(function()
        t.assert_equals(box.info.name, 'replica-name')
    end)
end

g.test_instance_name_new_uuid = function(lg)
    lg.replica:stop()
    local box_cfg = table.copy(lg.replica.box_cfg)
    box_cfg.bootstrap_strategy = 'legacy'
    box_cfg.replication = {server.build_listen_uri('master', lg.replica_set.id)}
    t.assert_equals(box_cfg.instance_name, 'replica-name')
    local new_replica = server:new({
        alias = 'new_replica',
        box_cfg = box_cfg,
    })
    new_replica:start({wait_until_ready = false})
    local logfile = fio.pathjoin(new_replica.workdir,
                                 new_replica.alias .. '.log')
    t.helpers.retrying({}, function()
        assert(new_replica:grep_log(
               'ER_INSTANCE_NAME_DUPLICATE: Duplicate replica name '..
               'replica%-name, already occupied', 1024, {filename = logfile}))
    end)
    new_replica:drop()
    lg.replica:start()
end
