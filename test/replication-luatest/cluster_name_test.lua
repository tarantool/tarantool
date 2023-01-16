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
        cluster_name = 'test-name',
    }
    lg.master = lg.replica_set:build_and_add_server({
        alias = 'master',
        box_cfg = box_cfg,
    })
    box_cfg.read_only = true
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
        t.assert_error_msg_contains(msg, box.cfg, {cluster_name = '123'})
        t.assert_error_msg_contains(msg, box.cfg, {cluster_name = '-abc'})
        t.assert_error_msg_contains(msg, box.cfg, {cluster_name = 'a~b'})

        msg = 'cluster name change'
        t.assert_error_msg_contains(msg, box.cfg, {cluster_name = 'test'})

        msg = 'type does not match'
        local _schema = box.space._schema
        t.assert_error_msg_contains(msg, _schema.replace, _schema,
                                    {'cluster_name', 'bad name'})
        t.assert_error_msg_contains(msg, _schema.replace, _schema,
                                    {'cluster_name', 100})
    end)
    lg.replica:exec(function()
        local msg = 'read-only instance'
        t.assert_error_msg_contains(msg, box.cfg, {cluster_name = 'test'})
    end)
end

g.test_cluster_name_basic = function(lg)
    local check_name_f = function()
        local _schema = box.space._schema
        t.assert_equals(box.info.cluster.name, 'test-name')
        t.assert_equals(_schema:get{'cluster_name'}.value, 'test-name')
    end
    lg.master:exec(check_name_f)
    lg.replica:exec(check_name_f)
end

g.test_cluster_rename = function(lg)
    --
    -- Drop the name.
    --
    lg.replica:exec(function()
        box.cfg{force_recovery = true}
    end)
    lg.master:exec(function()
        rawset(_G, 'last_event', {})
        rawset(_G, 'watcher', box.watch('box.id', function(_, event)
            _G.last_event = event
        end))
        box.cfg{
            force_recovery = true,
            cluster_name = box.NULL,
        }
        box.space._schema:delete{'cluster_name'}
        t.assert_equals(box.info.cluster.name, nil)
        t.assert_equals(_G.last_event.cluster_name, nil)
    end)
    lg.replica:wait_for_vclock_of(lg.master)
    lg.replica:exec(function()
        -- Box.info uses applied names, not cfg.
        t.assert_not_equals(box.cfg.cluster_name, nil)
        t.assert_equals(box.info.cluster.name, nil)
        box.cfg{cluster_name = box.NULL}
    end)
    --
    -- Replace with the same nil name.
    --
    lg.master:exec(function()
        local _schema = box.space._schema
        -- No tuple at all -> box.NULL.
        _schema:replace{'cluster_name', box.NULL}
        t.assert_equals(_G.last_event.cluster_name, nil)
        t.assert_equals(box.info.cluster.name, nil)

        -- Box.NULL -> nil.
        _schema:replace{'cluster_name'}
        t.assert_equals(_G.last_event.cluster_name, nil)
        t.assert_equals(box.info.cluster.name, nil)
    end)
    --
    -- Change nil -> not nil.
    --
    lg.replica:exec(function()
        box.cfg{force_recovery = false}
    end)
    lg.master:exec(function()
        -- Don't need force_recovery for that.
        box.cfg{
            force_recovery = false,
            cluster_name = 'test'
        }
        t.assert_equals(_G.last_event.cluster_name, 'test')
        t.assert_equals(box.info.cluster.name, box.cfg.cluster_name)
    end)
    lg.replica:wait_for_vclock_of(lg.master)
    lg.replica:exec(function()
        box.cfg{cluster_name = 'test'}
        t.assert_equals(box.info.cluster.name, box.cfg.cluster_name)
    end)
    --
    -- Change not nil -> same not nil.
    --
    lg.master:exec(function()
        box.space._schema:replace(box.space._schema:get{'cluster_name'})
        t.assert_equals(box.info.cluster.name, 'test')
    end)
    --
    -- Change not nil -> new not nil.
    --
    lg.replica:exec(function()
        box.cfg{force_recovery = true}
    end)
    lg.master:exec(function()
        -- The name is converted to the proper form automatically.
        box.cfg{
            force_recovery = true,
            cluster_name = 'TEST2'
        }
        t.assert_equals(box.cfg.cluster_name, 'test2')
        t.assert_equals(_G.last_event.cluster_name, 'test2')
        t.assert_equals(box.info.cluster.name, box.cfg.cluster_name)
    end)
    lg.replica:wait_for_vclock_of(lg.master)
    lg.replica:exec(function()
        t.assert_equals(box.cfg.cluster_name, 'test')
        box.cfg{cluster_name = 'TeSt2'}
        t.assert_equals(box.cfg.cluster_name, 'test2')
        t.assert_equals(box.info.cluster.name, box.cfg.cluster_name)
    end)
    --
    -- Cleanup.
    --
    lg.master:exec(function()
        _G.watcher:unregister()
        _G.watcher = nil
        _G.last_event = nil
        box.cfg{cluster_name = 'test-name'}
        box.cfg{force_recovery = false}
    end)
    lg.replica:wait_for_vclock_of(lg.master)
    lg.replica:exec(function()
        box.cfg{cluster_name = 'test-name'}
        box.cfg{force_recovery = false}
    end)
end

g.test_cluster_name_transactional = function(lg)
    lg.master:exec(function()
        t.assert_equals(box.info.cluster.name, 'test-name')
        box.cfg{force_recovery = true}
        box.begin()
        box.space._schema:replace{'cluster_name', 'new-name'}
        box.rollback()
        t.assert_equals(box.info.cluster.name, 'test-name')
        box.cfg{force_recovery = false}
    end)
end

g.test_cluster_name_bootstrap_mismatch = function(lg)
    --
    -- New replica has no cluster name, the master does. Then the replica uses
    -- the master's name, no conflict.
    --
    local box_cfg = table.copy(lg.replica.box_cfg)
    box_cfg.cluster_name = nil
    -- Don't bother with fullmesh. Master won't replicate from the new instance.
    box_cfg.bootstrap_strategy = 'legacy'
    local new_replica = server:new({
        alias = 'new_replica',
        box_cfg = box_cfg,
    })
    new_replica:start()
    new_replica:exec(function()
        t.assert_equals(box.cfg.cluster_name, nil)
        t.assert_equals(box.info.cluster.name, 'test-name')
        box.cfg{cluster_name = 'test-name'}
    end)
    new_replica:drop()
    --
    -- New replica has cluster name, the master doesn't.
    --
    lg.replica:exec(function()
        box.cfg{force_recovery = true}
    end)
    lg.master:exec(function()
        box.cfg{
            force_recovery = true,
            cluster_name = box.NULL
        }
        t.assert_equals(box.info.cluster.name, 'test-name')
        box.space._schema:delete{'cluster_name'}
        t.assert_equals(box.info.cluster.name, nil)
    end)
    box_cfg.cluster_name = 'test-name'
    new_replica = server:new({
        alias = 'new_replica',
        box_cfg = box_cfg,
    })
    new_replica:start({wait_until_ready = false})
    local logfile = fio.pathjoin(new_replica.workdir,
                                 new_replica.alias .. '.log')
    wait_for_death(new_replica)
    t.assert(new_replica:grep_log(
        'Cluster name mismatch: expected test%-name, got <no%-name>', 1024,
        {filename = logfile}))
    new_replica:drop()
    --
    -- Both master and replica have cluster names. But different ones.
    --
    lg.master:exec(function()
        box.cfg{cluster_name = 'test-name'}
        box.cfg{force_recovery = false}
        t.assert_equals(box.info.cluster.name, 'test-name')
    end)
    lg.replica:wait_for_vclock_of(lg.master)
    lg.replica:exec(function()
        box.cfg{force_recovery = false}
    end)
    box_cfg.cluster_name = 'new-name'
    new_replica = server:new({
        alias = 'new_replica',
        box_cfg = box_cfg,
    })
    new_replica:start({wait_until_ready = false})
    logfile = fio.pathjoin(new_replica.workdir, new_replica.alias .. '.log')
    wait_for_death(new_replica)
    t.assert(new_replica:grep_log(
        'Cluster name mismatch: expected new%-name, got test%-name', 1024,
        {filename = logfile}))
    new_replica:drop()
    lg.master:exec(function(replica_id)
        local own_id = box.info.id
        local _cluster = box.space._cluster
        for _, v in _cluster:pairs() do
            local id = v.id
            if id ~= own_id and id ~= replica_id then
                _cluster:delete{id}
            end
        end
    end, {lg.replica:get_instance_id()})
end

g.test_cluster_name_recovery_mismatch = function(lg)
    --
    -- Has name in WAL, other name in cfg.
    --
    local box_cfg = table.copy(lg.replica.box_cfg)
    box_cfg.cluster_name = 'new-name'
    -- Force recovery won't help.
    box_cfg.force_recovery = true
    lg.replica:restart({
        box_cfg = box_cfg,
    }, {wait_until_ready = false})
    local logfile = fio.pathjoin(lg.replica.workdir, lg.replica.alias .. '.log')
    wait_for_death(lg.replica)
    t.assert(lg.replica:grep_log(
        'Cluster name mismatch: expected new%-name, got test%-name', 1024,
        {filename = logfile}))
    --
    -- Has name in WAL, no name in cfg. Then the replica uses the saved name, no
    -- conflict.
    --
    box_cfg.cluster_name = nil
    -- Don't need force_recovery for this.
    box_cfg.force_recovery = nil
    lg.replica:restart({
        box_cfg = box_cfg,
    })
    lg.replica:exec(function()
        t.assert_equals(box.cfg.cluster_name, nil)
        t.assert_equals(box.info.cluster.name, 'test-name')
    end)
    --
    -- No name in WAL, has name in cfg.
    --
    lg.replica:exec(function()
        box.cfg{force_recovery = true}
    end)
    lg.master:exec(function()
        box.cfg{
            force_recovery = true,
            cluster_name = box.NULL,
        }
        box.space._schema:delete{'cluster_name'}
    end)
    lg.replica:wait_for_vclock_of(lg.master)
    box_cfg.cluster_name = 'new-name'
    -- Force recovery won't help.
    box_cfg.force_recovery = true
    lg.replica:restart({
        box_cfg = box_cfg,
    }, {wait_until_ready = false})
    wait_for_death(lg.replica)
    t.assert(lg.replica:grep_log(
        'Cluster name mismatch: expected new%-name, got <no%-name>', 1024,
        {filename = logfile}))
    box_cfg.cluster_name = nil
    box_cfg.force_recovery = nil
    lg.replica:restart({
        box_cfg = box_cfg,
    })
    --
    -- Restore the names.
    --
    lg.replica:exec(function()
        box.cfg{force_recovery = true}
    end)
    lg.master:exec(function()
        box.cfg{cluster_name = 'test-name'}
    end)
    --
    -- Master can't change the name on recovery either.
    --
    -- Has name in WAL, other name in cfg.
    --
    box_cfg = table.copy(lg.master.box_cfg)
    box_cfg.cluster_name = 'new-name'
    -- Force recovery won't help.
    box_cfg.force_recovery = true
    lg.master:restart({
        box_cfg = box_cfg,
    }, {wait_until_ready = false})
    logfile = fio.pathjoin(lg.master.workdir, lg.master.alias .. '.log')
    wait_for_death(lg.master)
    t.assert(lg.master:grep_log(
        'Cluster name mismatch: expected new%-name, got test%-name', 1024,
        {filename = logfile}))
    --
    -- No name in WAL, has name in cfg.
    --
    box_cfg.cluster_name = nil
    -- Don't need force_recovery for this.
    box_cfg.force_recovery = nil
    lg.master:restart({
        box_cfg = box_cfg,
    })
    lg.master:exec(function()
        box.cfg{
            force_recovery = true,
            cluster_name = box.NULL,
        }
        box.space._schema:delete{'cluster_name'}
    end)
    box_cfg.cluster_name = 'new-name'
    -- Force recovery won't help.
    box_cfg.force_recovery = true
    lg.master:restart({
        box_cfg = box_cfg,
    }, {wait_until_ready = false})
    wait_for_death(lg.master)
    t.assert(lg.master:grep_log(
        'Cluster name mismatch: expected new%-name, got <no%-name>', 1024,
        {filename = logfile}))
    box_cfg.cluster_name = nil
    box_cfg.force_recovery = nil
    lg.master:restart({
        box_cfg = box_cfg,
    })
    --
    -- Cleanup.
    --
    lg.master:exec(function()
        box.cfg{
            cluster_name = 'test-name',
            force_recovery = true,
        }
        box.cfg{force_recovery = false}
    end)
    lg.replica:wait_for_vclock_of(lg.master)
    lg.replica:exec(function()
        box.cfg{cluster_name = 'test-name'}
        box.cfg{force_recovery = false}
    end)
    lg.replica.box_cfg.cluster_name = 'test-name'
    lg.master.box_cfg.cluster_name = 'test-name'
end

--
-- See what happens when multiple cluster name updates arrive in one applier
-- batch and are applied without yields in parallel txns.
--
g.test_cluster_name_change_batch = function(lg)
    lg.replica:exec(function()
        box.cfg{
            force_recovery = true,
            replication = {},
        }
    end)
    lg.master:exec(function()
        box.cfg{force_recovery = true}
        t.assert_equals(box.cfg.cluster_name, 'test-name')
        for _ = 1, 3 do
            box.cfg{cluster_name = 'test-name-new'}
            box.cfg{cluster_name = 'test-name'}
        end
        box.cfg{force_recovery = false}
        t.assert_equals(box.info.cluster.name, 'test-name')
    end)
    lg.replica:exec(function(replication)
        box.cfg{replication = replication}
    end, {lg.replica.box_cfg.replication})
    lg.replica:wait_for_vclock_of(lg.master)
    lg.replica:exec(function()
        box.cfg{force_recovery = false}
        t.assert_equals(box.info.cluster.name, 'test-name')
    end)
end
