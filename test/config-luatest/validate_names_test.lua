local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')
local uuid = require('uuid')

local g_names = t.group('config_recovery_with_names_set')

--
-- before_all trigger starts cluster in order to generate snapshot
-- files, which will be used to test, how confidata works.
--
local function cluster_initialize(g, with_names)
    local replicaset_uuid = uuid.str()
    local instance_1_uuid = uuid.str()
    local instance_2_uuid = uuid.str()

    g.replica_set = replica_set:new({})
    local box_cfg = {
        replicaset_name = with_names and 'replicaset-001' or nil,
        replicaset_uuid = replicaset_uuid,
        replication = {
            server.build_listen_uri('instance-001', g.replica_set.id),
            server.build_listen_uri('instance-002', g.replica_set.id),
        },
    }

    box_cfg.instance_name = with_names and 'instance-001' or nil
    box_cfg.instance_uuid = instance_1_uuid
    local instance_1 = g.replica_set:build_and_add_server(
        {alias = 'instance-001', box_cfg = box_cfg})

    box_cfg.instance_name = with_names and 'instance-002' or nil
    box_cfg.instance_uuid = instance_2_uuid
    box_cfg.read_only = true
    g.replica_set:build_and_add_server(
        {alias = 'instance-002', box_cfg = box_cfg})

    g.replica_set:start()
    g.replica_set:wait_for_fullmesh()
    for _, replica in ipairs(g.replica_set.servers) do
        replica:exec(function()
            box.snapshot()
        end)
    end

    g.config = {
        credentials = {
            users = {
                guest = {
                    roles = {'super'},
                },
            },
        },

        snapshot = {
            dir = instance_1.workdir,
        },

        iproto = {
            listen = {{uri = 'unix/:./{{ instance_name }}.iproto'}},
        },

        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        database = { replicaset_uuid = replicaset_uuid },
                        instances = {
                            ['instance-001'] = {
                                database = {instance_uuid = instance_1_uuid},
                            },
                            ['instance-002'] = {
                                database = {instance_uuid = instance_2_uuid},
                            },
                        },
                    },
                },
            },
        },
    }

    g.server = server:new({alias = 'server'})
    g.server:start()
    g.server:exec(function()
        rawset(_G, 'box_cfg_clean', function()
            rawset(_G, 'old_box_cfg', box.cfg)
            box.cfg = function() end
        end)
        rawset(_G, 'box_cfg_restore', function()
            box.cfg = _G.old_box_cfg
        end)
        -- In order not to call require on every test
        rawset(_G, 'cluster_config', require('internal.config.cluster_config'))
        rawset(_G, 'configdata', require('internal.config.configdata'))
    end)
end

g_names.before_all(function(g)
    cluster_initialize(g, true)
end)

g_names.after_all(function(g)
    g.replica_set:drop()
    g.server:drop()
end)

g_names.test_replicaset_uuid_mismatch = function(g)
    g.server:exec(function(config)
        _G.box_cfg_clean()
        local rs = config.groups['group-001'].replicasets['replicaset-001']
        rs.database.replicaset_uuid = require('uuid').str()

        local iconfig = _G.cluster_config:instantiate(config, 'instance-001')
        t.assert_error_msg_contains('Replicaset UUID mismatch', function()
            return _G.configdata.new(iconfig, config, 'instance-001')
        end)
        _G.box_cfg_restore()
    end, {g.config})
end

g_names.test_replicaset_name_mismatch = function(g)
    g.server:exec(function(config)
        _G.box_cfg_clean()
        local rs = config.groups['group-001'].replicasets['replicaset-001']
        config.groups['group-001'].replicasets['replicaset-002'] = rs
        config.groups['group-001'].replicasets['replicaset-001'] = nil

        local iconfig = _G.cluster_config:instantiate(config, 'instance-001')
        t.assert_error_msg_contains('Replicaset name mismatch', function()
            return _G.configdata.new(iconfig, config, 'instance-001')
        end)
        _G.box_cfg_restore()
    end, {g.config})
end

g_names.test_instance_uuid_mismatch = function(g)
    g.server:exec(function(config)
        _G.box_cfg_clean()
        -- Workdir has snapshot files for instance-001.
        local iconfig = _G.cluster_config:instantiate(config, 'instance-002')
        t.assert_error_msg_contains('Instance UUID mismatch', function()
            return _G.configdata.new(iconfig, config, 'instance-002')
        end)
        _G.box_cfg_restore()
    end, {g.config})
end

g_names.test_instance_name_mismatch = function(g)
    g.server:exec(function(config)
        _G.box_cfg_clean()
        -- UUID is correct, but name is not
        local rs = config.groups['group-001'].replicasets['replicaset-001']
        rs.instances['instance-003'] = rs.instances['instance-001']
        rs.instances['instance-001'] = nil

        local iconfig = _G.cluster_config:instantiate(config, 'instance-003')
        t.assert_error_msg_contains('Instance name mismatch', function()
            return _G.configdata.new(iconfig, config, 'instance-003')
        end)
        _G.box_cfg_restore()
    end, {g.config})
end

local g_no_names = t.group('config_recovery_without_names_set')

g_no_names.before_all(function(g)
    cluster_initialize(g, false)
end)

g_no_names.after_all(function(g)
    g.replica_set:stop()
end)

g_no_names.test_missing_names = function(g)
    g.server:exec(function(config)
        _G.box_cfg_clean()
        -- Name should be missing even if no uuid was passed.
        local rs = config.groups['group-001'].replicasets['replicaset-001']
        rs.instances['instance-002'].database = nil

        local iconfig = _G.cluster_config:instantiate(config, 'instance-001')
        local configdata = _G.configdata.new(iconfig, config, 'instance-001')
        local missing = configdata:missing_names()
        t.assert_not_equals(missing._peers['instance-001'], nil)
        t.assert_not_equals(missing._peers['instance-002'], nil)
        t.assert_not_equals(missing['replicaset-001'], nil)
        _G.box_cfg_restore()
    end, {g.config})
end

g_no_names.test_instance_uuid_require = function(g)
    g.server:exec(function(config)
        _G.box_cfg_clean()
        local rs = config.groups['group-001'].replicasets['replicaset-001']
        rs.instances['instance-001'].database = nil

        local iconfig = _G.cluster_config:instantiate(config, 'instance-001')
        local msg = 'Instance name for instance-001 is not set in snapshot'
        t.assert_error_msg_contains(msg, function()
            return _G.configdata.new(iconfig, config, 'instance-001')
        end)
        _G.box_cfg_restore()
    end, {g.config})
end

g_no_names.test_replicaset_uuid_require = function(g)
    g.server:exec(function(config)
        _G.box_cfg_clean()
        local rs = config.groups['group-001'].replicasets['replicaset-001']
        rs.database = nil

        local iconfig = _G.cluster_config:instantiate(config, 'instance-001')
        local msg = 'Replicaset name for replicaset-001 is not set in snapshot'
        t.assert_error_msg_contains(msg, function()
            return _G.configdata.new(iconfig, config, 'instance-001')
        end)
        _G.box_cfg_restore()
    end, {g.config})
end
