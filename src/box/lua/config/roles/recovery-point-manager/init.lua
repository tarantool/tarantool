--
-- The recovery-point-manager role. It drives the recovery point manager API
-- (box.backup.recovery_point.manager_*) from the declarative configuration: for
-- every configured manager it resolves the effective backend and options and
-- creates, reconfigures or drops the manager accordingly.
--
local utils = require('internal.utils')
local schema = require('experimental.config.utils.schema')
local BACKEND_PREFIX = 'roles.recovery-point-manager.backends.'

--------------------------------------------------------------------------------
-- Config
--------------------------------------------------------------------------------

local function create_schema()
    return schema.record({
        timeout = schema.scalar({type = 'number'}),
        by = schema.record({
            interval = schema.scalar({type = 'number'}),
        }),
    })
end

-- Overrides for global scope of configuration.
local manager_schema = schema.record({
    backend = schema.scalar({type = 'string'}),
    backend_cfg = schema.scalar({type = 'any'}),
    create = create_schema(),
})

local config_schema = schema.new('recovery-point-manager', schema.record({
    -- Role-level defaults, inherited by every manager.
    backend = schema.scalar({type = 'string'}),
    -- Per-backend-type config, keyed by backend type (net-replicaset, ...).
    backends_cfg = schema.map({
        key = schema.scalar({type = 'string'}),
        value = schema.scalar({type = 'any'}),
    }),
    create = create_schema(),
    -- The managers to run, keyed by a user-chosen name. Omitted -> a single
    -- `default` manager; an explicit empty map -> no managers.
    managers = schema.map({
        key = schema.scalar({type = 'string'}),
        value = manager_schema,
    }),
}, {
    validate = function(data, w)
        local managers = data.managers ~= nil and
                         data.managers or {default = {}}
        for name, manager in pairs(managers) do
            manager = manager ~= nil and manager or {}
            if manager.backend == nil and data.backend == nil then
                w.error(('backend is not set for the %q recovery point ' ..
                         'manager'):format(name))
            end
        end
    end,
}))

--------------------------------------------------------------------------------
-- Effective configuration
--------------------------------------------------------------------------------

--
-- A schemaless deep-merge (right-hand side winning, nested string-keyed tables
-- merged recursively) of the whole effective config.
--
local merge_schema = schema.new('rpm-merge', schema.scalar({type = 'any'}))

--
-- The per-manager effective configuration: {name -> {backend_type, backend_cfg,
-- create_interval, timeout}}. The manager's un-keyed backend_cfg is normalized
-- into the role-level keyed shape (backends_cfg[backend_type]); the whole
-- is then deep-merged with the role-level defaults in one pass (the manager
-- winning). The chosen backend's config is then backends_cfg[backend].
--
local function effective_managers(cfg)
    cfg = cfg or {}
    local managers = cfg.managers ~= nil and cfg.managers or {default = {}}
    local defaults = {
        backend = cfg.backend,
        backends_cfg = cfg.backends_cfg,
        create = cfg.create,
    }
    local res = {}
    for name, manager in pairs(managers) do
        local options = manager ~= nil and manager or {}
        local backend_type = options.backend or cfg.backend
        local normalized = {backend = options.backend, create = options.create}
        if options.backend_cfg ~= nil and backend_type ~= nil then
            normalized.backends_cfg = {[backend_type] = options.backend_cfg}
        end
        local eff = merge_schema:merge(defaults, normalized)
        local backends_cfg = eff.backends_cfg ~= nil and eff.backends_cfg or {}
        local create = eff.create ~= nil and eff.create or {}
        res[name] = {
            backend_type = backend_type,
            backend_cfg = backend_type ~= nil and
                          backends_cfg[backend_type] or {},
            create_interval = create.by ~= nil and create.by.interval or nil,
            timeout = create.timeout,
        }
    end
    return res
end

local function backend_module(name, backend_type)
    -- The backend presence is enforced by the config schema.
    assert(backend_type)
    local ok, backend = pcall(require, BACKEND_PREFIX .. backend_type)
    if not ok then
        error(string.format(
            'recovery point manager %q failed loading backend %q: %s',
            name, backend_type, backend))
    end
    utils.check_param(backend, backend_type, 'table')
    utils.check_param(backend.config, backend_type .. '.config', 'table')
    utils.check_param(backend.config.validate,
                      backend_type .. ' config.validate', 'function')
    return backend
end

--------------------------------------------------------------------------------
-- Role
--------------------------------------------------------------------------------

-- The set of manager names this role has created, so a reload can tell the
-- managers to reconfigure (vs create) and drop the ones that disappeared.
local created = {}

local function validate(cfg)
    cfg = cfg or {}
    -- Normalize empty managers to {} so the schema sees plain records.
    if cfg.managers ~= nil then
        local managers = {}
        for name, manager in pairs(cfg.managers) do
            managers[name] = manager ~= nil and manager or {}
        end
        cfg = table.copy(cfg)
        cfg.managers = managers
    end
    config_schema:validate(cfg)
    -- Each manager must resolve to a known backend, and its effective config
    -- must pass that backend's own validation.
    for name, manager in pairs(effective_managers(cfg)) do
        local backend = backend_module(name, manager.backend_type)
        backend.config.validate(manager.backend_cfg)
    end
end

local function apply(cfg)
    local recovery_point = box.backup.recovery_point
    local desired = effective_managers(cfg)

    -- Drop the managers that disappeared from the configuration.
    for name in pairs(created) do
        if desired[name] == nil then
            recovery_point.manager_drop(name)
            created[name] = nil
        end
    end

    -- Create the new managers and reconfigure the existing ones.
    for name, manager in pairs(desired) do
        local manager_cfg = {
            backend = backend_module(name, manager.backend_type),
            backend_cfg = manager.backend_cfg,
            create_interval = manager.create_interval,
            timeout = manager.timeout,
        }
        if created[name] == nil then
            recovery_point.manager_create(name, manager_cfg)
            created[name] = true
        else
            recovery_point.managers[name]:cfg(manager_cfg)
        end
    end
end

local function stop()
    local recovery_point = box.backup.recovery_point
    for name in pairs(created) do
        recovery_point.manager_drop(name)
        created[name] = nil
    end
end

return {
    validate = validate,
    apply = apply,
    stop = stop,
}
