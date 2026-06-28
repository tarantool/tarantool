local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group()

local REDUNDANCY = 3
local ROLE = 'roles.recovery-point-manager'

local function make_config(rpm_cfg, opts)
    opts = opts or {}
    local sharding_role = {
        privileges = {{permissions = {'execute'}, universe = true}},
    }
    local builder = cbuilder:new()
    :set_global_option('credentials.roles.sharding', sharding_role)
    :set_global_option('credentials.users.storage.roles', {'sharding'})
    :set_global_option('credentials.users.storage.password', 'secret_storage')
    :set_global_option('credentials.users.backup.password', 'secret_backup')
    :set_global_option('credentials.users.backup.privileges', {{
        permissions = {'execute'},
        lua_call = {'box.backup.recovery_point.create'},
    }})
    :set_global_option('credentials.users.noperm.password', 'secret_noperm')
    :set_global_option('iproto.advertise.sharding.login', 'storage')
    :set_global_option('roles_cfg', {[ROLE] = rpm_cfg})
    :use_group('test')
    local router = builder:use_replicaset('router')
    if opts.role ~= false then
        router:set_replicaset_option('roles', {ROLE})
    end
    router:add_instance('router', {})
    builder:use_replicaset('storage')
           :set_replicaset_option('replication.failover', 'manual')
           :set_replicaset_option('leader', 'storage1')
    for i = 1, REDUNDANCY do
        builder:add_instance('storage' .. i, {})
    end
    return builder:config()
end

-- The sorted names of the managers the role created on the router.
local function manager_names()
    return g.cluster.router:exec(function()
        local names = {}
        for name in pairs(box.backup.recovery_point.managers) do
            table.insert(names, name)
        end
        table.sort(names)
        return names
    end)
end

-- The worker fiber id of a manager (to tell a kept fiber from a restarted one).
local function worker_id(name)
    return g.cluster.router:exec(function(name)
        return box.backup.recovery_point.managers[name].worker:id()
    end, {name})
end

-- Wait until a manager has created at least one point.
local function wait_point(name)
    g.cluster.router:exec(function(name)
        t.helpers.retrying({timeout = 30}, function()
            local m = box.backup.recovery_point.managers[name]
            t.assert_not_equals(m, nil)
            t.assert_not_equals(m:info().last_point, nil)
        end)
    end, {name})
end

-- Wait until a manager's failure alert surfaces in config:info().alerts.
local function wait_alert(name)
    g.cluster.router:exec(function(name)
        t.helpers.retrying({timeout = 60}, function()
            local found = false
            for _, alert in ipairs(require('config'):info().alerts) do
                if alert.message:find(name, 1, true) and
                   alert.message:find('failed to create a recovery point',
                                      1, true) then
                    found = true
                end
            end
            t.assert(found)
        end)
    end, {name})
end

local function reload(rpm_cfg, opts)
    g.cluster:reload(make_config(rpm_cfg, opts))
end

-- Find a recovery point's row on the storage leader by its exact (replica_id,
-- lsn). The returned timestamp is a lossy seconds double, not the raw
-- nanosecond primary key, so it cannot be used for a by-key lookup.
local function find_point(point)
    return g.cluster.storage1:exec(function(point)
        for _, row in box.space._recovery_point:pairs() do
            if row.replica_id == point.replica_id and
               row.lsn == point.lsn then
                return row:tomap({names_only = true})
            end
        end
    end, {point})
end

g.before_all(function()
    g.cluster = cluster:new(make_config({managers = {}}), nil,
                            {auto_cleanup = false})
    g.cluster:start()
end)

g.after_all(function()
    if g.cluster ~= nil then
        g.cluster:drop()
        g.cluster = nil
    end
end)

g.after_each(function()
    t.assert_equals(manager_names(), {})
end)

-- Omitting `managers` yields a single `default` manager inheriting the
-- role-level backend and backends_cfg; it creates points on the target leader.
g.test_default_manager = function()
    reload({
        backend = 'net-replicaset',
        create = {by = {interval = 0.1}},
        backends_cfg = {['net-replicaset'] = {target = 'storage',
                                              login = 'backup'}},
    })
    t.assert_equals(manager_names(), {'default'})
    wait_point('default')
    local info = g.cluster.router:exec(function()
        return box.backup.recovery_point.managers.default:info()
    end)
    t.assert_equals(info.backend.backend_type, 'net-replicaset')
    t.assert_equals(next(info.alerts), nil)

    -- The point landed on the storage leader.
    t.assert_not_equals(find_point(info.last_point), nil)

    reload({managers = {}})
end

-- A named manager's `backend_cfg` deep-merges onto the global
-- `backends_cfg[<backend>]` (manager wins): here login comes from the global
-- defaults and target from the manager, and the merged identity connects.
g.test_named_manager_merge = function()
    reload({
        create = {by = {interval = 0.1}},
        backends_cfg = {['net-replicaset'] = {login = 'backup'}},
        managers = {
            cs_1 = {backend = 'net-replicaset',
                    backend_cfg = {target = 'storage'}},
        },
    })
    t.assert_equals(manager_names(), {'cs_1'})
    wait_point('cs_1')

    reload({managers = {}})
end

-- An explicit empty `managers` map means zero managers (distinct from omitting
-- it, which is the default manager covered above).
g.test_managers_empty = function()
    reload({
        backend = 'net-replicaset',
        backends_cfg = {['net-replicaset'] = {target = 'storage'}},
        managers = {},
    })
    t.assert_equals(manager_names(), {})
end

-- A manager that resolves to no backend, or an unknown one, is rejected at
-- configuration validation -- the reload fails and the old config is kept.
g.test_invalid_backend_rejected = function()
    t.assert_error_msg_contains('backend is not set', function()
        reload({managers = {cs_1 = {}}})
    end)
    t.assert_error_msg_contains('failed loading backend "nope"', function()
        reload({backend = 'nope', managers = {cs_1 = {}}})
    end)
    -- The backend's own validation runs too (target is mandatory).
    t.assert_error_msg_contains('target is mandatory', function()
        reload({backend = 'net-replicaset', managers = {cs_1 = {}}})
    end)
    t.assert_equals(manager_names(), {})
end

-- A backend module that loads but does not conform to the interface, or that
-- fails to load at all, is rejected at validation with a clear, specific error
-- rather than an opaque "attempt to index a nil value" or a reasonless failure.
g.test_malformed_backend_rejected = function()
    g.cluster.router:exec(function()
        local t = require('luatest')
        local role = require('roles.recovery-point-manager')
        local PREFIX = 'roles.recovery-point-manager.backends.'
        local function validate_with(backend_type)
            role.validate({backend = backend_type, managers = {cs_1 = {}}})
        end

        package.loaded[PREFIX .. 'noconfig'] = {}
        t.assert_error_msg_contains('config should be a table', function()
            validate_with('noconfig')
        end)
        package.loaded[PREFIX .. 'novalidate'] = {config = {}}
        t.assert_error_msg_contains('config.validate should be a function',
                                    function() validate_with('novalidate') end)
        package.preload[PREFIX .. 'nontable'] = function() end
        t.assert_error_msg_contains('should be a table', function()
            validate_with('nontable')
        end)

        package.preload[PREFIX .. 'loaderror'] = function()
            error('synthetic load failure')
        end
        t.assert_error_msg_contains('synthetic load failure', function()
            validate_with('loaderror')
        end)

        -- Do not leak the injected modules to other tests.
        for _, name in ipairs({'noconfig', 'novalidate', 'nontable',
                               'loaderror'}) do
            package.loaded[PREFIX .. name] = nil
            package.preload[PREFIX .. name] = nil
        end
    end)
end

-- How-to: register and drive a custom backend. A third-party backend (say,
-- aeon) is any module requireable as
-- roles.recovery-point-manager.backends.<type> that exposes:
--   * config.validate(cfg) -- validates the backend_cfg;
--   * new(cfg) -> an instance with create_point(opts) (compulsory, opts carries
--     label/timeout) and optional drop()/info().
-- Here we register one in package.preload and let the role drive it end to end.
g.test_custom_backend = function()
    local BACKEND = 'roles.recovery-point-manager.backends.custom'
    g.cluster.router:exec(function(module_name)
        package.preload[module_name] = function()
            local instance_methods = {}
            local instance_mt = {__index = instance_methods}

            function instance_methods.create_point(self, opts)
                table.insert(self.created, opts.label)
                return {label = opts.label}
            end

            function instance_methods.info(self)
                return {backend_type = 'custom', tag = self.tag,
                        count = #self.created}
            end

            local M = {}
            M.config = {
                validate = function(cfg)
                    assert(type(cfg.tag) == 'string',
                           'custom backend: tag must be a string')
                end,
            }
            function M.new(cfg)
                return setmetatable({tag = cfg.tag, created = {}}, instance_mt)
            end
            return M
        end
    end, {BACKEND})

    -- Enable it exactly like a built-in backend: name it in `backend` and pass
    -- its config in `backends_cfg[<type>]`.
    reload({
        backend = 'custom',
        create = {by = {interval = 0.1}},
        backends_cfg = {custom = {tag = 'demo'}},
    })
    t.assert_equals(manager_names(), {'default'})
    wait_point('default')

    -- The role drives the custom backend: its info() surfaces under the
    -- manager's info().backend, and it has created points.
    local backend = g.cluster.router:exec(function()
        return box.backup.recovery_point.managers.default:info().backend
    end)
    t.assert_equals(backend.backend_type, 'custom')
    t.assert_equals(backend.tag, 'demo')
    t.assert_gt(backend.count, 0)

    reload({managers = {}})
    g.cluster.router:exec(function(module_name)
        package.preload[module_name] = nil
        package.loaded[module_name] = nil
    end, {BACKEND})
end

-- On reconfiguration the role keeps unchanged managers running (same fiber),
-- applies changed ones in place, and drops the ones that disappeared.
g.test_reconfigure = function()
    reload({
        create = {by = {interval = 0.1}},
        backends_cfg = {['net-replicaset'] = {target = 'storage',
                                              login = 'backup'}},
        managers = {
            cs_1 = {backend = 'net-replicaset'},
            cs_2 = {backend = 'net-replicaset'},
        },
    })
    t.assert_equals(manager_names(), {'cs_1', 'cs_2'})
    wait_point('cs_1')
    wait_point('cs_2')
    local id1 = worker_id('cs_1')

    -- A no-op reload restarts nothing: same managers, same fibers.
    reload({
        create = {by = {interval = 0.1}},
        backends_cfg = {['net-replicaset'] = {target = 'storage',
                                              login = 'backup'}},
        managers = {
            cs_1 = {backend = 'net-replicaset'},
            cs_2 = {backend = 'net-replicaset'},
        },
    })
    t.assert_equals(manager_names(), {'cs_1', 'cs_2'})
    t.assert_equals(worker_id('cs_1'), id1)

    -- Drop cs_2 and change cs_1's identity to one that cannot create a point.
    -- cs_1's fiber is kept, but the new backend_cfg takes effect (it starts
    -- failing), while cs_2 is gone.
    reload({
        create = {by = {interval = 0.1}},
        backends_cfg = {['net-replicaset'] = {target = 'storage'}},
        managers = {
            cs_1 = {backend = 'net-replicaset',
                    backend_cfg = {login = 'noperm'},
                    create = {timeout = 2}},
        },
    })
    t.assert_equals(manager_names(), {'cs_1'})
    t.assert_equals(worker_id('cs_1'), id1)
    wait_alert('cs_1')

    reload({managers = {}})
end

-- Disabling the role drops all of its managers (stop() is called with no args).
g.test_stop = function()
    reload({
        backend = 'net-replicaset',
        backends_cfg = {['net-replicaset'] = {target = 'storage',
                                              login = 'backup'}},
    })
    t.assert_equals(manager_names(), {'default'})

    -- Remove the role from the router.
    reload({managers = {}}, {role = false})
    t.assert_equals(manager_names(), {})

    -- Restore the baseline: the role enabled again with no managers.
    reload({managers = {}})
end

-- A failing manager raises an alert in config:info().alerts past the failure
-- threshold; fixing its identity clears the alert on recovery.
g.test_alerts = function()
    reload({
        backends_cfg = {['net-replicaset'] = {target = 'storage'}},
        create = {by = {interval = 0.1}, timeout = 2},
        managers = {cs_1 = {backend = 'net-replicaset',
                            backend_cfg = {login = 'noperm'}}},
    })
    wait_alert('cs_1')

    -- Repair the identity: the manager recovers and the alert clears.
    reload({
        backends_cfg = {['net-replicaset'] = {target = 'storage',
                                              login = 'backup'}},
        create = {by = {interval = 0.1}},
        managers = {cs_1 = {backend = 'net-replicaset'}},
    })
    wait_point('cs_1')
    g.cluster.router:exec(function()
        t.helpers.retrying({timeout = 30}, function()
            for _, alert in ipairs(require('config'):info().alerts) do
                t.assert_not_str_contains(alert.message,
                                          'failed to create a recovery point')
            end
        end)
    end)

    reload({managers = {}})
end
