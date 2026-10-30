local log = require('internal.config.utils.log')

-- {{{ General-purpose utils

-- {'a', 'b', 'c'} => {a = true, b = true, c = true}
local function array2set(t)
    local res = {}
    for _, v in ipairs(t) do
        res[v] = true
    end
    return res
end

-- }}} General-purpose utils

-- {{{ box status notifier

local _box_status_watcher

-- Invoke the given callback when box.status event occurs.
--
-- It reacts on RO->RW, RW->RO switch and the data dictionary
-- schema version update.
local function box_status_notifier_start(cb)
    if _box_status_watcher ~= nil then
        return
    end

    _box_status_watcher = box.watch('box.status', function(_status)
        cb()
    end)
end

-- Stop to call the callback and free resources.
local function box_status_notifier_stop()
    if _box_status_watcher == nil then
        return
    end

    _box_status_watcher:unregister()
    _box_status_watcher = nil
end

-- }}} box status notifier

-- {{{ Hold last config

local _last_cfg

local function save_cfg(cfg)
    _last_cfg = cfg
end

local function load_cfg()
    return _last_cfg
end

-- }}} Hold last config

-- {{{ Issue or drop an alert

-- The key is to leave only the latest alert if several failed
-- attempts to perform the expelling were done.
--
-- Also, it allows to drop the alert using the key.
local _alert_key = 'autoexpel_error'

local function set_alert(reason)
    -- require() it here to avoid a circular dependency.
    local config = require('config')

    local message = ('autoexpel failed (reload the configuration to retry): ' ..
        '%s'):format(reason)

    config._aboard:set({
        type = 'warn',
        message = message,
    }, {
        key = _alert_key,
    })
end

local function drop_alert()
    -- require() it here to avoid a circular dependency.
    local config = require('config')

    config._aboard:drop(_alert_key)
end

-- }}} Issue or drop an alert

-- {{{ Autoexpel domain-specific logic

local function _autoexpel_precondition(cfg)
    -- Can't perform DDL on a read-only instance.
    if box.info.ro then
        return false
    end

    -- Refuse to do automatic replicaset management operations
    -- during the upgrading procedure, because it seems dangerous.
    --
    -- There is a warning about a non-upgraded database schema and
    -- it shouldn't be a surprise for a user that the
    -- autoexpelling is not performed in this state.
    local current_version = box.internal.dd_version()
    local latest_version = box.internal.latest_dd_version()
    if current_version < latest_version then
        return false
    end

    -- If autoexpelling is stopped but we reach this code somehow
    -- (a race between the watcher unregistering and the watcher
    -- event?), just don't do anything.
    if cfg == nil then
        return false
    end

    return true
end

local function _autoexpel_txn(cfg)
    local collected = {}

    for _, tuple in box.space._cluster:pairs() do
        local name = tuple[3]
        -- NB: The check for presence in `peers` is always true
        -- for the current instance, because an attempt to apply a
        -- configuration without the current instance fails on the
        -- validation step (see config/init.lua:_store).
        local m = type(name) == 'string' and
            cfg.peers[name] == nil and
            name:startswith(cfg.prefix)
        if m then
            log.verbose('autoexpel: instance %q is marked to expel', name)
            table.insert(collected, tuple)
        end
    end

    for _, tuple in ipairs(collected) do
        local pk = {tuple[1]}
        local name = tuple[3]
        box.space._cluster:delete(pk)
        log.info('autoexpel: instance %q is expelled', name)
    end
end

-- Save to call in RO or if other criteria to perform the
-- autoexpel don't met: no-op in the case.
local function autoexpel_do()
    local cfg = load_cfg()

    -- This way we don't bother about spurious calls and can just
    -- call autoexpel_do() on any reconfiguration or box.status
    -- change.
    if not _autoexpel_precondition(cfg) then
        return
    end

    -- Deletes from _cluster are performed in a transaction.
    local ok, err = pcall(box.atomic, _autoexpel_txn, cfg)

    -- If an error occurs, report it to the user using the alerts
    -- mechanism.
    --
    -- The alert is dropped on the next configuration reloading or
    -- if the next expelling attempt is successful (it may be
    -- triggered by a box.status watcher event).
    if ok then
        drop_alert()
    else
        set_alert(err)
    end
end

-- }}} Autoexpel domain-specific logic

-- {{{ On new configuration

local function apply(config)
    local configdata = config._configdata

    local user_cfg = configdata:get('replication.autoexpel',
        {use_default = true})

    if user_cfg == nil or not user_cfg.enabled then
        log.verbose('autoexpel: stopped (disabled by the configuration)')
        box_status_notifier_stop()
        save_cfg(nil)
        return
    end

    log.verbose('autoexpel: started (enabled by the configuration)')

    assert(user_cfg.enabled)
    assert(user_cfg.by == 'prefix')
    assert(user_cfg.prefix)

    save_cfg({
        prefix = user_cfg.prefix,
        peers = array2set(configdata:peers()),
    })
    autoexpel_do()
    box_status_notifier_start(autoexpel_do)

    log.verbose('autoexpel: a new config is sent to the worker fiber')
end

-- }}} On new configuration

return {
    name = 'autoexpel',
    apply = apply,
}
