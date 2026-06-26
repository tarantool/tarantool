--
-- Sharding-agnostic recovery point manager. A manager periodically asks an
-- injected backend to create a recovery point: it owns the scheduling, the
-- retry/backoff and the alerting, while the backend decides where and how a
-- point is actually created.
--
-- The API is installed onto box.backup.recovery_point from the minimal Lua
-- sources, so it is available before the rest of the backup API and before
-- box.cfg() (for example, on an unconfigured vshard router).
--
local log = require('log')
local uuid = require('uuid')
local fiber = require('fiber')
local digest = require('digest')
local utils = require('internal.utils')

-- Number of consecutive failures after which the manager raises an alert.
local FAILURE_ALERT_THRESHOLD = 5
-- The backoff starts here and doubles on each consecutive failure.
local BACKOFF_MIN = 1
-- Defaults for the optional cfg members.
local DEFAULT_CREATE_INTERVAL = 600
local DEFAULT_TIMEOUT = 300

local MANAGER_TEMPLATE = {
    -- Manager name: the key in `managers` and the middle segment of point ids.
    name = nil,
    -- Backend module (cfg.backend); the manager only calls backend.new().
    backend = nil,
    -- Resolved backend options, passed to backend.new().
    backend_cfg = nil,
    -- The owned backend instance returned by backend.new().
    backend_instance = nil,
    -- Seconds between successful attempts.
    create_interval = DEFAULT_CREATE_INTERVAL,
    -- Per-attempt timeout passed to the backend.
    timeout = DEFAULT_TIMEOUT,
    -- The last recovery point the manager created (the backend's payload),
    -- shown in the manager:info().
    last_point = nil,
    -- The config alerts namespace 'recovery_point_manager.<name>' info().alerts
    -- is mirrored into (manager + backend alerts). nil when config is
    -- unavailable (for example in an application thread).
    alert_namespace = nil,
    -- Number of consecutive failed attempts; drives the backoff and the alert.
    consecutive_failures = 0,
    -- The scheduling fiber running manager_loop().
    worker = nil,
}

local managers = {}

--------------------------------------------------------------------------------
-- Option validation
--------------------------------------------------------------------------------

local check_param = utils.check_param
local check_param_table = utils.check_param_table

local function check_positive_number(value)
    if type(value) ~= 'number' or value <= 0 then
        return false, 'positive number'
    end
    return true
end

--
-- Each entry is {type, is_compulsory}: `type` is a Lua type name or a validator
-- function (whatever check_param_table accepts); `is_compulsory` marks a field
-- that must be present. Consumed by check_interface().
--
local MANAGER_CFG_TEMPLATE = {
    -- backend is not compulsory here in order to allow partial update of the
    -- options during reconfiguration. The manager requirement that a backend
    -- exists is enforced by backend_new() on the first configuration.
    backend = {type = 'table'},
    backend_cfg = {type = 'table'},
    timeout = {type = check_positive_number},
    create_interval = {type = check_positive_number},
}

local BACKEND_TEMPLATE = {
    new = {type = 'function', is_compulsory = true},
}

local BACKEND_INSTANCE_TEMPLATE = {
    create_point = {type = 'function', is_compulsory = true},
    drop = {type = 'function'},
    info = {type = 'function'},
}

--
-- check_param_table() consumes a plain name -> type table. Derive one per
-- TEMPLATE once at load time instead of rebuilding it on every check.
--
local function template_to_types(template)
    local types = {}
    for name, spec in pairs(template) do
        types[name] = spec.type
    end
    return types
end

local MANAGER_CFG_TYPES = template_to_types(MANAGER_CFG_TEMPLATE)
local BACKEND_TYPES = template_to_types(BACKEND_TEMPLATE)
local BACKEND_INSTANCE_TYPES = template_to_types(BACKEND_INSTANCE_TEMPLATE)

--
-- Validate `obj` (labelled `what`) against a TEMPLATE and its precomputed
-- types. Enforces `is_compulsory` fields, checks the remaining for types.
--
-- Only the interface members named in the template are validated; any other
-- fields the object carries are ignored. A backend module has a `config` next
-- to its `new`, and a backend instance holds its own state (a connection, ...)
-- beside its methods -- so the object cannot be validated as a closed option
-- table.
--
local function check_interface(obj, what, template, types)
    check_param(obj, what, 'table')
    local members = {}
    for name in pairs(template) do
        members[name] = obj[name]
    end
    check_param_table(members, types)
    for name, spec in pairs(template) do
        if spec.is_compulsory then
            check_param(obj[name], what .. '.' .. name, spec.type)
        end
    end
end

--------------------------------------------------------------------------------
-- Backend
--------------------------------------------------------------------------------

local function backend_drop(backend_instance)
    assert(backend_instance)
    if backend_instance.drop then
        pcall(backend_instance.drop, backend_instance)
    end
end

local function backend_new(backend, name, backend_cfg)
    check_interface(backend, 'cfg.backend', BACKEND_TEMPLATE, BACKEND_TYPES)
    local instance = backend.new(backend_cfg)
    check_interface(instance, string.format('%q backend instance', name),
                    BACKEND_INSTANCE_TEMPLATE, BACKEND_INSTANCE_TYPES)
    return instance
end

--------------------------------------------------------------------------------
-- Manager
--------------------------------------------------------------------------------

--
-- The default label for a recovery point: <instance>.<manager>.<uuid>, with a
-- fresh uuid on each call. The instance segment is the instance name, its uuid
-- when the name is unset, or is omitted entirely when the instance is
-- unconfigured (box.info is also absent in an application thread).
--
local function manager_default_label(manager)
    local info = box.info
    local instance = info ~= nil and (info.name ~= box.NULL and info.name
        or info.uuid ~= uuid.NULL and info.uuid)
    if instance then
        return string.format('%s.%s.%s', instance, manager.name, uuid.str())
    end
    return string.format('%s.%s', manager.name, uuid.str())
end

--
-- Create a recovery point now via the backend. opts may carry a `label` (the
-- manager's default is used when absent) and a `timeout` (manager.timeout when
-- absent). Returns the backend payload, or nil + err. Does not interrupt the
-- scheduling loop.
--
local function manager_create_point(manager, opts)
    opts = opts or {}
    return manager.backend_instance:create_point({
        label = opts.label or manager_default_label(manager),
        timeout = opts.timeout or manager.timeout,
    })
end

--
-- The manager's own alert for the consecutive failures. Returns nil while the
-- manager is below the failure threshold.
--
local function manager_alert(manager)
    if manager.consecutive_failures < FAILURE_ALERT_THRESHOLD then
        return nil
    end
    return {
        message = string.format(
            'recovery point manager %q: failed to create a recovery point ' ..
            '%d times in a row', manager.name, manager.consecutive_failures),
    }
end

--
-- The manager's introspection: the last point it created, the backend's own
-- info (under the `backend` key) and the alerts. The manager's own alert is
-- computed on the fly and the backend's alerts are lifted out of its info, so
-- every alert surfaces together under info.alerts rather than under
-- info.backend.alerts.
--
local function manager_info(manager)
    local alerts = {}
    local own_alert = manager_alert(manager)
    if own_alert ~= nil then
        table.insert(alerts, own_alert)
    end
    local info = {last_point = manager.last_point}
    local backend_instance = manager.backend_instance
    if backend_instance.info ~= nil then
        -- The backend info() is third-party code; a raise must not propagate --
        -- it would kill the scheduling fiber (which syncs alerts on every
        -- iteration) or a public :info() call. Surface the failure as an alert.
        local ok, backend_info = pcall(backend_instance.info, backend_instance)
        if not ok then
            table.insert(alerts, {message = string.format(
                'recovery point manager %q: backend info() failed: %s',
                manager.name, backend_info)})
        elseif backend_info ~= nil then
            if backend_info.alerts ~= nil then
                for _, alert in ipairs(backend_info.alerts) do
                    table.insert(alerts, alert)
                end
                backend_info.alerts = nil
            end
            info.backend = backend_info
        end
    end
    info.alerts = alerts
    return info
end

--
-- Resync the manager's alerts (its own plus the backend's) into the config
-- alerts namespace, when the manager has one.
--
local function manager_sync_alert(manager)
    local namespace = manager.alert_namespace
    if namespace == nil then
        return
    end
    namespace:clear()
    for _, alert in ipairs(manager_info(manager).alerts) do
        local ok, err = pcall(namespace.add, namespace, alert)
        if not ok then
            log.error('recovery point manager %q: dropped an invalid alert: %s',
                      manager.name, err)
        end
    end
end

--
-- The time in seconds to sleep after failure. 1, 2, 4, ... seconds, capped at
-- create_interval / 10 and never below 1s.
--
local function manager_backoff(manager)
    local backoff = BACKOFF_MIN * 2 ^ (manager.consecutive_failures - 1)
    local cap = math.max(BACKOFF_MIN, manager.create_interval / 10)
    return math.min(backoff, cap)
end

--
-- gh-12876: math.random is not seeded, so it cannot be used. The function
-- returns a random fraction in [0, 1) from urandom.
--
local function random_unit()
    -- Four urandom bytes, each in [0, 255].
    local b1, b2, b3, b4 = digest.urandom(4):byte(1, 4)
    -- Pack them into a 32-bit integer in [0, 2^32 - 1] and normalize to [0, 1).
    return (((b1 * 256 + b2) * 256 + b3) * 256 + b4) / 2 ^ 32
end

local function manager_loop(manager)
    -- Decorrelate managers across the cluster (for example, one per DC) with a
    -- random offset in [0, interval), the way checkpoints are decorrelated.
    local delay = random_unit() * manager.create_interval
    log.info('recovery point manager %s started: interval %d s, delay %d s',
              manager.name, manager.create_interval, delay)
    fiber.sleep(delay)
    while true do
        -- The backend should return the point, or nil + err on its own failure.
        local ok, res, err = pcall(manager_create_point, manager,
                                   {timeout = manager.timeout})
        fiber.testcancel()
        local sleep_time
        if ok and res ~= nil then
            manager.consecutive_failures = 0
            manager.last_point = res
            sleep_time = manager.create_interval
        else
            local reason = ok and err or res
            manager.consecutive_failures = manager.consecutive_failures + 1
            log.error('recovery point manager %q: failed to create a ' ..
                      'recovery point (%d in a row): %s', manager.name,
                      manager.consecutive_failures, reason)
            sleep_time = manager_backoff(manager)
        end
        manager_sync_alert(manager)
        fiber.sleep(sleep_time)
    end
end

local function manager_stop(manager)
    if manager.worker ~= nil and manager.worker:status() ~= 'dead' then
        manager.worker:cancel()
    end
    manager.worker = nil
    backend_drop(manager.backend_instance)
    manager.backend_instance = nil
end

--
-- (Re)configure the manager in place and make sure its fiber is running. cfg is
-- a partial update: an option absent from it keeps the current value. The fiber
-- is never restarted -- the loop reads create_interval and timeout live. Only a
-- backend change (a different module or backend_cfg, or the very first
-- configuration) rebuilds the owned instance.
--
local function manager_cfg(manager, cfg)
    check_interface(cfg, 'cfg', MANAGER_CFG_TEMPLATE, MANAGER_CFG_TYPES)
    -- 1. Rebuild and set the backend when it changed. The new instance is built
    -- before the old one is dropped (so a broken backend leaves the manager
    -- running on its old one), and a create_point caught mid-flight on the old
    -- one just fails and retries.
    local backend = cfg.backend or manager.backend
    local backend_cfg = cfg.backend_cfg or manager.backend_cfg
    if manager.backend_instance == nil or backend ~= manager.backend or
       not table.equals(backend_cfg, manager.backend_cfg) then
        local instance = backend_new(backend, manager.name, backend_cfg)
        local old_instance = manager.backend_instance
        manager.backend = backend
        manager.backend_cfg = backend_cfg
        manager.backend_instance = instance
        manager.consecutive_failures = 0
        if old_instance ~= nil then
            backend_drop(old_instance)
        end
    end
    -- 2. Set the remaining options, then resync the alert.
    manager.create_interval = cfg.create_interval or manager.create_interval
    manager.timeout = cfg.timeout or manager.timeout
    manager_sync_alert(manager)
    -- 3. Start the scheduling fiber on the first configuration.
    if manager.worker == nil then
        manager.worker = fiber.new(manager_loop, manager)
        manager.worker:name(
            'recovery_point_manager.' .. manager.name, {truncate = true})
    end
end

local function manager_drop(manager)
    manager_stop(manager)
    if manager.alert_namespace ~= nil then
        manager.alert_namespace:clear()
    end
    if managers[manager.name] == manager then
        managers[manager.name] = nil
    end
end

local manager_mt = {
    __index = {
        cfg = manager_cfg,
        drop = manager_drop,
        info = manager_info,
        create_point = manager_create_point,
    },
}

--------------------------------------------------------------------------------
-- API
--------------------------------------------------------------------------------

--
-- Create a named manager and start its scheduling fiber.
--
--   @string name                - the manager name (key in `managers`).
--   @table  cfg
--   @table  cfg.backend         - COMPULSORY. A backend module; the manager
--                                 calls backend.new(cfg.backend_cfg), owns the
--                                 instance and drops it on drop.
--   @table  cfg.backend_cfg     - the resolved backend options, passed to
--                                 new().
--   @number cfg.create_interval - seconds between attempts (positive, default
--                                 DEFAULT_CREATE_INTERVAL).
--   @number cfg.timeout         - per-attempt timeout (positive, default
--                                 DEFAULT_TIMEOUT).
--
local function manager_create(name, cfg)
    check_param(name, 'name', 'string')
    if managers[name] ~= nil then
        error(string.format('recovery point manager %q already exists',
                             name), 2)
    end

    local manager = setmetatable(table.deepcopy(MANAGER_TEMPLATE), manager_mt)
    manager.name = name
    -- The config module lives in the main Lua sources while the manager lives
    -- in the minimal ones, so require it lazily and only when it is loaded (an
    -- application thread has no config).
    local ok, config = pcall(require, 'config')
    if ok then
        manager.alert_namespace = config:new_alerts_namespace(
            'recovery_point_manager.' .. name)
    end
    manager_cfg(manager, cfg)
    managers[name] = manager
    return manager
end

local function manager_drop_by_name(name)
    check_param(name, 'name', 'string')
    local manager = managers[name]
    if manager ~= nil then
        manager_drop(manager)
    end
end

-- Install the API onto box.backup.recovery_point. This runs from the minimal
-- Lua sources, before the rest of the backup API, so the table is created here
-- if absent.
box.backup = box.backup or {}
box.backup.recovery_point = box.backup.recovery_point or {}
box.backup.recovery_point.managers = managers
box.backup.recovery_point.manager_create = manager_create
box.backup.recovery_point.manager_drop = manager_drop_by_name
