-- Alert board.

local datetime = require('datetime')
local log = require('internal.config.utils.log')

-- Set an alert.
--
-- Optional `opts.key` declares an unique identifier of the alert.
-- Attempt to set a new alert with the same key replaces the
-- existing alert.
--
-- The key can be used to drop the alert using :drop().
--
-- The `alert` is a table of the following structure.
--
-- {
--     type = 'warn' or 'error',
--     message = <string>,
-- }
--
-- More fields may be placed into the table and all these fields
-- are returned from :alerts() (except ones that start from the
-- underscore character).
--
-- The alert board takes an ownership of the `alert` table and
-- modifies it.
--
-- A timestamp is saved on the :set() method call to show it in
-- an :alerts() result.
local function aboard_set(self, alert, opts)
    assert(alert.type == 'error' or alert.type == 'warn')
    if alert.type == 'error' then
        log.error(alert.message)
    else
        log.warn(alert.message)
    end
    alert.timestamp = datetime.now()

    local key
    if opts == nil or opts.key == nil then
        key = self._next_key
        self._next_key = self._next_key + 1
    else
        key = opts.key
    end

    self._alerts[key] = alert
end

-- Return an alert pointed by the given `key` or nil.
local function aboard_get(self, key)
    return self._alerts[key]
end

-- Drop an alert pointed by the given `key`.
--
-- The function is no-op if the alert doesn't exist.
--
-- The `on_drop` callback (see the aboard.new() function) is
-- called if the alert had existed.
local function aboard_drop(self, key)
    if self._alerts[key] == nil then
        return
    end
    self._alerts[key] = nil
    if self._on_drop ~= nil then
        self._on_drop(self, key)
    end
end

-- Drop alerts that fit the given criteria.
--
-- | local function filter_f(key, alert)
-- |     return <boolean>
-- | end
--
-- The filter function is called on each alert. An alert is
-- dropped if the function returns `true`.
--
-- The `on_drop` callback (see the aboard.new() function) is
-- called for each of the dropped alert.
local function aboard_drop_if(self, filter_f)
    local to_drop = {}
    for key, alert in pairs(self._alerts) do
        if filter_f(key, alert) then
            table.insert(to_drop, key)
        end
    end

    for _, key in ipairs(to_drop) do
        self:drop(key)
    end
end

-- Traverse all alerts and apply the provided callback function to each of them.
local function aboard_each(self, callback)
    for key, alert in pairs(self._alerts) do
        callback(key, alert)
    end
end

-- Drop all the alerts.
--
-- The `on_drop` callback is NOT called.
local function aboard_clean(self)
    self._alerts = {}
    self._next_key = 1
end

-- Serialize the alerts to show them to a user.
--
-- The alerts are sorted by a time of the :set() method call and
-- returned as an array-like table.
--
-- Fields of the alert object that start from the underscore
-- character are NOT shown.
local function aboard_alerts(self)
    -- Don't return alert keys.
    local alerts = {}
    for _, alert in pairs(self._alerts) do
        -- Don't show fields that start from an underscore.
        local serialized = {}
        for k, v in pairs(alert) do
            if not k:startswith('_') then
                serialized[k] = v
            end
        end
        table.insert(alerts, serialized)
    end
    -- Sort by timestamps.
    table.sort(alerts, function(a, b)
        return a.timestamp < b.timestamp
    end)
    return alerts
end

-- Return one of three possible statuses:
--
-- * ready
-- * check_warnings
-- * check_errors
local function aboard_status(self)
    local status = 'ready'

    for _, alert in pairs(self._alerts) do
        assert(alert.type == 'error' or alert.type == 'warn')
        if alert.type == 'error' then
            return 'check_errors'
        end

        status = 'check_warnings'
    end

    return status
end

-- Return `true` if there are no alerts, `false` otherwise.
local function aboard_is_empty(self)
    return next(self._alerts) == nil
end

-- {{{ Alerts namespaces

local namespace_methods = {}
local namespace_mt = {
    __index = namespace_methods,
}

local function namespace_selfcheck(self, method_name)
    if type(self) ~= 'table' or getmetatable(self) ~= namespace_mt then
        local fmt_str = 'Use alerts_namespace:%s(<...>) ' ..
                        'instead of alerts_namespace.%s(<...>)'
        error(fmt_str:format(method_name, method_name), 0)
    end
end

-- Process an alert before adding it to the alert board.
-- The function adds the namespace name to the alert,
-- sets the default alert type to 'warn',
-- and removes all fields that start from an underscore.
local function process_alert(namespace, alert)
    assert(type(alert) == 'table', 'alert must be a table')
    assert(alert.type == nil or alert.type == 'warn',
           'alert.type must be nil or "warn"')

    local a = table.copy(alert)
    a.type = 'warn'

    for k, _ in pairs(a) do
        if k:startswith('_') then
            a[k] = nil
        end
    end

    a._namespace = namespace._name

    return a
end

local function process_key(namespace, key)
    assert(type(key) == 'string', 'alert key must be a string')
    assert(not key:find(':'), 'alert key cannot contain a colon')

    -- The key is prefixed with the namespace name to avoid conflicts.
    return ("%s:%s"):format(namespace._name, key)
end

function namespace_methods.add(self, alert)
    namespace_selfcheck(self, 'add')
    local a = process_alert(self, alert)
    self._aboard:set(a)
end

function namespace_methods.set(self, key, alert)
    namespace_selfcheck(self, 'set')
    local a = process_alert(self, alert)
    self._aboard:set(a, {key = process_key(self, key)})
end

function namespace_methods.unset(self, key)
    namespace_selfcheck(self, 'unset')
    self._aboard:drop(process_key(self, key))
end

function namespace_methods.clear(self)
    namespace_selfcheck(self, 'clear')
    self._aboard:drop_if(function(_key, alert)
        return alert._namespace == self._name
    end)
end

local function namespace_new(self, name)
    assert(type(name) == 'string', 'namespace name must be a string')
    assert(not name:find(':'), 'namespace name cannot contain a colon')

    return setmetatable({
        _aboard = self,
        _name = name,
    }, namespace_mt)
end

-- }}} Alerts namespaces

local mt = {
    __index = {
        set = aboard_set,
        get = aboard_get,
        each = aboard_each,
        drop = aboard_drop,
        drop_if = aboard_drop_if,
        clean = aboard_clean,
        alerts = aboard_alerts,
        status = aboard_status,
        is_empty = aboard_is_empty,
        new_namespace = namespace_new,
    },
}

-- Create a new alert board.
--
-- The `on_drop` callback is called on the :drop() method call.
-- It is NOT called on the :clean() call.
--
-- The callback should have the following prototype.
--
-- | local function on_drop_cb(aboard, key)
-- |     <...>
-- | end
local function new(opts)
    local on_drop
    if opts ~= nil then
        on_drop = opts.on_drop
    end

    return setmetatable({
        -- _alerts is a table of the following structure.
        --
        -- {
        --     [key1] = {
        --         type = 'warn' or 'error',
        --         message = <string>,
        --         timestamp = <datetime object>,
        --         <..arbitrary fields..>
        --     },
        --     [key2] = {
        --         <...>
        --     },
        --     <...>
        -- }
        --
        -- The key can be a string or a number.
        _alerts = {},
        _next_key = 1,
        _on_drop = on_drop,
    }, mt)
end

return {
    new = new,
}
