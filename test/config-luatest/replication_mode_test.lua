-- tags: parallel

local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local instance_config = require('internal.config.instance_config')
local cluster_config = require('internal.config.cluster_config')
local configdata = require('internal.config.configdata')
local g = t.group()

local function make_config(mode, compat_value, failover)
    return cbuilder:new()
        :set_global_option('compat.box_cfg_replication_mode', compat_value)
        :set_global_option('replication.mode', mode)
        :set_global_option('replication.failover', failover or 'off')
        :add_instance('i-001', {})
        :config()
end

local function validate(config)
    cluster_config:validate(config)
    local iconfig = cluster_config:instantiate(config, 'i-001')
    return configdata.new(iconfig, config, 'i-001')
end

g.test_scopes = function()
    for _, option in ipairs({'replication.mode',
                              'compat.box_cfg_replication_mode'}) do
        local value = option == 'replication.mode' and 'sync' or 'new'
        for _, scope in ipairs({'global', 'group', 'replicaset', 'instance'}) do
            local config = cbuilder:new():add_instance('i-001', {}):config()
            local group = config.groups['group-001']
            local rs = group.replicasets['replicaset-001']
            local scopes = {global = config, group = group, replicaset = rs,
                instance = rs.instances['i-001']}
            instance_config:set(scopes[scope], option, value)
            if scope == 'group' or scope == 'instance' then
                t.assert_error_msg_contains('must not be present in the ' ..
                    scope .. ' scope', cluster_config.validate,
                    cluster_config, config)
            else
                cluster_config:validate(config)
            end
        end
    end
end

g.test_desired_validation = function()
    for _, value in ipairs({false, true}) do
        local config = cbuilder:new(make_config('sync', 'new', 'supervised'))
            :set_global_option(
                'failover.replicasets.replicaset-001.synchro_mode', value)
            :config()
        t.assert_error_msg_contains('synchro_mode is forbidden',
                                    validate, config)
    end
    for _, failover in ipairs({'off', 'manual'}) do
        t.assert_error_msg_contains('requires replication.failover', validate,
            make_config('sync', 'new', failover))
    end
    t.assert_error_msg_contains('conflicts with election mode', validate,
        make_config('async', 'new', 'election'))
end

for _, values in ipairs({{'sync', 'new', 'election'},
                         {'sync', 'new', 'supervised'},
                         {'async', 'new', 'supervised'},
                         {'sync', 'old', 'off'}}) do
    g['test_startup_' .. table.concat(values, '_')] = function()
        local c = cluster:new(make_config(unpack(values)))
        c:start()
    end
end

g.test_peer_mismatch = function()
    local config = make_config('async', 'old')
    local iconfig = table.deepcopy(cluster_config:instantiate(config, 'i-001'))
    instance_config:set(iconfig, 'replication.mode', 'sync')
    t.assert_error_msg_contains('must agree across replicaset',
        configdata.new, iconfig, config, 'i-001')
end

g.test_anonymous_election = function()
    local config = cbuilder:new(make_config('sync', 'new', 'election'))
        :add_instance('i-002', {replication = {election_mode = 'voter'}})
        :add_instance('i-003', {replication = {
            anon = true, election_mode = 'off',
        }})
        :config()
    local c = cluster:new(config)
    c:start()
    c['i-001']:wait_for_election_leader()
    c['i-001']:exec(function()
        box.schema.space.create('s'):create_index('pk')
        box.space.s:insert{1}
        box.ctl.replication_sync{include_anon = true, timeout = 60}
    end)
    c['i-003']:exec(function() t.assert_equals(box.space.s:select(), {{1}}) end)
end

g.test_supervised_helper = function()
    for _, compat_value in ipairs({'old', 'new'}) do
        for _, mode in ipairs({'sync', 'async'}) do
            for _, legacy in ipairs({true, false}) do
                local config = {
                    compat = {box_cfg_replication_mode = compat_value},
                    replication = {mode = mode},
                    failover = {replicasets = {r = {synchro_mode = legacy}}},
                }
                local expected = legacy
                if compat_value == 'new' then expected = mode == 'sync' end
                t.assert_equals(instance_config:is_supervised_sync(config, 'r'),
                                expected)
            end
        end
    end
end

g.test_reload = function()
    local config = make_config('async', 'old')
    local c = cluster:new(config)
    c:start()
    local sync = make_config('sync', 'new', 'supervised')
    c:reload(sync)
    c['i-001']:exec(function()
        t.assert(require('compat').box_cfg_replication_mode:is_new())
        t.assert_equals(box.cfg.replication_mode, 'sync')
        t.assert_equals(box.cfg.election_mode, 'manual')
        box.ctl.promote()
        box.schema.space.create('s'):create_index('pk')
        box.space.s:insert{1}
        box.ctl.txn_sync()
    end)
    local async = make_config('async', 'new', 'supervised')
    c:reload(async)
    c['i-001']:exec(function()
        t.assert_equals(box.cfg.election_mode, 'off')
        t.assert(box.info.ro)
        box.ctl.demote()
    end)
    -- Reverse compat while changing to an election mode invalid for new async.
    local legacy = make_config('async', 'old', 'supervised')
    legacy = cbuilder:new(legacy):set_global_option(
        'failover.replicasets.replicaset-001.synchro_mode', true):config()
    c:reload(legacy)
    c['i-001']:exec(function()
        t.assert(require('compat').box_cfg_replication_mode:is_old())
        t.assert_equals(box.cfg.election_mode, 'manual')
        t.assert_equals(box.space.s.is_sync, false)
    end)
    -- Compat is applied before box.cfg(). Prepare a valid sync configuration
    -- under old compatibility before enabling the new behavior again.
    legacy = cbuilder:new(legacy)
        :set_global_option('replication.mode', 'sync'):config()
    c:reload(legacy)
    c:reload(sync)
end

--
-- Compat is applied before the group. A rejected mode change preserves
-- elections and leadership, while the preceding compat update remains.
--
g.test_failed_reload_keeps_applied_settings = function()
    local config = make_config('sync', 'old', 'supervised')
    config = cbuilder:new(config):set_global_option(
        'failover.replicasets.replicaset-001.synchro_mode', true):config()
    local c = cluster:new(config)
    c:start()
    c['i-001']:exec(function()
        box.ctl.promote()
        box.schema.space.create('s', {is_sync = true}):create_index('pk')
        box.cfg{replication_synchro_quorum = 2}
        box.begin()
        box.space.s:insert{1}
        box.commit{wait = 'submit'}
        box.ctl.wal_sync()
    end)
    local async = make_config('async', 'new', 'supervised')
    async = cbuilder:new(async):set_global_option(
        'replication.synchro_quorum', 2):config()
    t.assert_error_msg_contains('empty synchronous queue', c.reload, c, async)
    c['i-001']:exec(function()
        t.assert(require('compat').box_cfg_replication_mode:is_new())
        t.assert_equals(box.cfg.replication_mode, 'sync')
        t.assert_equals(box.cfg.election_mode, 'manual')
        t.assert_equals(box.info.election.state, 'leader')
        t.assert_equals(box.space.s.is_sync, nil)
        box.cfg{replication_synchro_quorum = 1}
        box.ctl.txn_sync()
    end)
    c:reload(make_config('async', 'new', 'supervised'))
end

--
-- The regular compat applier checks the current box.cfg(), before the new
-- mode and election settings are applied. Stage those settings under old
-- compatibility when the current combination cannot enable the new behavior.
--
g.test_reload_compat_validation = function()
    local c = cluster:new(make_config('sync', 'old'))
    c:start()
    local config = make_config('sync', 'new', 'supervised')
    t.assert_error_msg_contains('requires election_mode', c.reload, c, config)
    c['i-001']:exec(function()
        t.assert(require('compat').box_cfg_replication_mode:is_old())
        t.assert_equals(box.cfg.replication_mode, 'sync')
        t.assert_equals(box.cfg.election_mode, 'off')
    end)
    local legacy = cbuilder:new(make_config('sync', 'old', 'supervised'))
        :set_global_option(
            'failover.replicasets.replicaset-001.synchro_mode', true):config()
    c:reload(legacy)
    c:reload(config)
    c['i-001']:exec(function()
        t.assert(require('compat').box_cfg_replication_mode:is_new())
        t.assert_equals(box.cfg.replication_mode, 'sync')
        t.assert_equals(box.cfg.election_mode, 'manual')
    end)
end
