local fiber = require('fiber')
local net_box = require('net.box')

-- The constant is copy-paste from tarantool/src/lua/socket.lua.
local TIMEOUT_INFINITY = 500 * 365 * 86400

-- Raise a net.replicaset specific error.
local function rs_error(err_code, replicaset_name, payload)
    local args = payload and table.copy(payload) or {}
    args.code = err_code
    args.replicaset = replicaset_name
    box.error(args, 2)
end

local replicaset_methods = {}

local replicaset_mt = {
    __index = replicaset_methods,
}

-- Check that the arguments table comply table template.
-- Template key-value pairs show possible options and their required types -
-- a string concatenated from several types with reasonable delimiter.
local function check_options(tbl, tmpl, name)
    if type(tbl) ~= 'table' then
        box.error(box.error.ILLEGAL_PARAMS, name .. ' expected to be a map')
    end
    for k, v in pairs(tmpl) do
        if not string.find(v, type(tbl[k])) then
            local fmt = 'option \'%s\' of %s is \'%s\' while must be \'%s\''
            box.error(box.error.ILLEGAL_PARAMS,
                      string.format(fmt, k, name, type(tbl[k]), v))
        end
    end
    for k in pairs(tbl) do
        if tmpl[k] == nil then
            box.error(box.error.ILLEGAL_PARAMS,
                      'unexpected option ' .. k .. ' in ' .. name, 2)
        end
    end
end

-- Check that the first argument is a valid replicaset.
local function replicaset_check(replicaset, method)
    if getmetatable(replicaset) ~= replicaset_mt then
        local fmt = 'Use replicaset:%s(...) instead of replicaset.%s(...)'
        box.error(box.error.ILLEGAL_PARAMS, string.format(fmt, method, method))
    end
end

-- Wait for the leader of replicaset to be known, limited with given timeout.
-- The leader is the only known RW replica.
-- Note1. This method does not wait for all peers to connect and send status.
-- For example it will successfully return if one replica is known as RW while
-- two others have not send status event. This is done for availability of
-- replicaset connection: if one or more replica instance are down it should
-- be possible to proceed requests with replicaset remains.
-- Note2. Due to asynchronous nature of replicaset connection it is possible
-- that the known leader is not actually a leader or perhaps not even RW.
-- Returns remaining timeout.
function replicaset_methods:_wait_leader(timeout)
    if self.status_count.rw == 1 or timeout < 0 then
        return timeout
    end
    local deadline = fiber.clock() + timeout
    repeat
        self.wait_status_cond:wait(timeout)
        timeout = deadline - fiber.clock()
    until self.status_count.rw == 1 or timeout < 0
    return timeout
end

-- Allowed options and their types in cfg of connect.
local call_opts_template = {
    timeout = 'number or nil',
    is_async = 'boolean or nil',
}

-- Call a function on replicaset leader if the leader is known.
-- Args and opts are exactly the same as in net.box.call(..).
-- Note that known leader can become not leader while the call is inprogress,
-- so a certain error (like ER_READONLY) may happen.
-- NO_WRITEABLE and MORE_THAN_ONE_WRITEABLE may be thrown.
function replicaset_methods:call_leader(func, args, opts)
    replicaset_check(self, 'call_leader')
    opts = opts or {}
    check_options(opts, call_opts_template, 'call options')
    local timeout = opts.timeout or TIMEOUT_INFINITY
    timeout = self:_wait_leader(timeout)
    if self.status_count.rw == 1 then
        -- Try to avoid excess copy in the most expected case.
        local call_opts = opts
        if opts.timeout and opts.timeout ~= timeout then
            call_opts = table.copy(opts)
            call_opts.timeout = timeout
        end
        return self.leader_instance.conn:call(func, args, call_opts)
    elseif self.status_count.rw == 0 then
        rs_error(box.error.REPLICASET_NO_WRITABLE,
                 self.replicaset_name)
    else
        rs_error(box.error.REPLICASET_MORE_THAN_ONE_WRITABLE,
                 self.replicaset_name)
    end
end

local watcher_methods = {}

local watcher_mt = {
    __index = watcher_methods,
}

-- Check that the first argument is a valid watcher.
local function watcher_check(watcher, method)
    if getmetatable(watcher) ~= watcher_mt then
        local fmt = 'Use watcher:%s(...) instead of watcher.%s(...)'
        error(string.format(fmt, method, method))
    end
end

-- Stop watching the key.
function watcher_methods.unregister(watcher)
    watcher_check(watcher, 'unregister')
    if watcher.handle then
        watcher.handle:unregister()
        watcher.handle = nil
    end
    if watcher.replicaset then
        watcher.replicaset.leader_watchers[watcher] = nil
        watcher.replicaset = nil
    end
    watcher.key = nil
    watcher.func = nil
end

-- Get closure with `key`, `value` arguments that calls user watcher function
-- with extended signature (with the third `instance_name` argument).
-- In addition the closure uses guard preventing concurrent execution of func.
local function wrap_watcher_function(watcher, instance)
    local instance_name = instance.instance_name
    return function(key, value)
        while watcher.is_executing do
            watcher.execute_cond:wait()
        end
        -- Check that the watcher was not unregistered while we was waiting.
        if watcher.handle then
            watcher.is_executing = true
            -- Don't care about error and result, it doesn't affect anything.
            pcall(watcher.func, key, value, instance_name)
            watcher.is_executing = false
        end
        watcher.execute_cond:signal()
    end
end

-- Subscribe to `key` event on the leader of the replicaset.
-- When the key value is updated the callback `func` will be called like that:
-- func(key, value, instance_name).
function replicaset_methods:watch_leader(key, func)
    replicaset_check(self, 'watch_leader')
    local watcher = {
        replicaset = self,
        key = key,
        func = func,
        -- Guard that prevents concurrent execution of func.
        is_executing = false,
        execute_cond = fiber.cond(),
    }
    local leader_instance = self.leader_instance
    if leader_instance then
        local wwfunc = wrap_watcher_function(watcher, leader_instance)
        watcher.handle = leader_instance.conn:watch(watcher.key, wwfunc)
    end
    self.leader_watchers[watcher] = watcher
    return setmetatable(watcher, watcher_mt)
end

-- Unregister all leader watchers from old leader (leader count becomes not 1).
local function replicaset_unbind_watchers(replicaset)
    for _, watcher in pairs(replicaset.leader_watchers) do
        watcher.handle:unregister()
        watcher.handle = nil
    end
end

-- Register all leader watchers to the new leader (leader count becomes 1).
local function replicaset_rebind_watchers(replicaset)
    local leader_instance = replicaset.leader_instance
    for _, watcher in pairs(replicaset.leader_watchers) do
        local wwfunc = wrap_watcher_function(watcher, leader_instance)
        watcher.handle = leader_instance.conn:watch(watcher.key, wwfunc)
    end
end

-- Close all backend connections.
function replicaset_methods:close()
    replicaset_check(self, 'close')
    for _, instance in pairs(self.instances) do
        instance.conn:close()
    end
end

-- Callback that called if the status of an instance is (possibly) changed.
-- Called from watcher ('ro' or 'rw') or from on_disconnect ('unknown').
local function on_instance_status_change(replicaset, instance, status)
    if status == instance.status then
        return
    end
    replicaset.wait_status_cond:broadcast()
    local status_count = replicaset.status_count
    status_count[instance.status] = status_count[instance.status] - 1
    instance.status = status
    status_count[instance.status] = status_count[instance.status] + 1

    if status_count.rw ~= 1 and replicaset.leader_instance then
        replicaset.leader_instance = nil
        replicaset_unbind_watchers(replicaset)
    elseif status_count.rw == 1 and not replicaset.leader_instance then
        if status == 'rw' then
            replicaset.leader_instance = instance
        else
            for _, search_instance in pairs(replicaset.instances) do
                if search_instance.status == 'rw' then
                    replicaset.leader_instance = search_instance
                    break
                end
            end
        end
        replicaset_rebind_watchers(replicaset)
    end
end

-- Allowed options and their types in cfg of connect.
local connect_cfg_template = {
    name = 'string or nil',
    instances = 'table',
    reconnect_timeout = 'number or nil',
}

-- Allowed options and their types in cfg.instances.x of connect.
local connect_cfg_instance_template = {
    endpoint = 'string or table',
    reconnect_timeout = 'number or nil',
}

-- Connect by config.
-- The config must have `instances`, a map of instance configs by their names.
-- The instance config must have an `endpoint` member.
-- All other options are passed to net.box.connect function; all root options
-- are inherited by each instance.
local function connect_by_cfg(cfg)
    check_options(cfg, connect_cfg_template, 'connect cfg')
    if not next(cfg.instances) then
        error('cfg.instances expected to be a non-empty map')
    end
    for instance_name, instance_cfg in pairs(cfg.instances) do
        check_options(instance_cfg, connect_cfg_instance_template,
                    'cfg.instances.' .. instance_name)
    end

    local replicaset = {
        -- Optional name of replicaset.
        replicaset_name = cfg.name,
        -- Array of instances, see below.
        instances = {},
        -- Count of instances by every possible status.
        status_count = {rw = 0, ro = 0, unknown = 0},
        -- Reference to the only RW instance (if there is only).
        leader_instance = nil,
        -- Watchers that were added with 'watch_leader' method.
        leader_watchers = {},
        -- Conditional var that broadcast if any instance status is changed.
        wait_status_cond = fiber.cond(),
    }
    for instance_name, instance_cfg in pairs(cfg.instances) do
        local connect_cfg = {
            wait_connected = false,
            fetch_schema = false,
            reconnect_after = instance_cfg.reconnect_timeout or
                              cfg.reconnect_timeout,
        }
        local conn = net_box.connect(instance_cfg.endpoint, connect_cfg)
        local instance = {
            -- Obligatory name of the instance.
            instance_name = instance_name,
            -- Net.box connection to that instance.
            conn = conn,
            -- 'unknown' by default, will be updated to 'ro' or 'rw' when
            -- the remote instance will send status event to local watcher,
            -- and will be set to 'unknown' again on disconnect.
            status = 'unknown',
        }
        table.insert(replicaset.instances, instance)
        replicaset.status_count.unknown = replicaset.status_count.unknown + 1
        -- Make weak references to replicaset/instance to avoid reference cycle.
        local weak_ref = setmetatable({
            replicaset = replicaset,
            instance = instance,
        }, {__mode = 'v'})
        conn:watch('box.status', function(_key, value)
            local status = value.is_ro and 'ro' or 'rw'
            if weak_ref.replicaset and weak_ref.instance then
                on_instance_status_change(weak_ref.replicaset,
                                          weak_ref.instance, status)
            end
        end)
        conn:on_disconnect(function()
            if weak_ref.replicaset and weak_ref.instance then
                on_instance_status_change(weak_ref.replicaset,
                                          weak_ref.instance, 'unknown')
            end
        end)
    end
    return setmetatable(replicaset, replicaset_mt)
end

-- Allowed options and their types in cfg.instances.x of connect by name.
local connect_by_name_cfg_template = {
    reconnect_timeout = 'number or nil',
}

-- Connect by replicaset name.
-- Optional config is passed to every net.box.connect function.
-- REPLICASET_NOT_FOUND error may be thrown.
local function connect_by_rs_name(replicaset_name, cfg)
    cfg = cfg or {}
    check_options(cfg, connect_by_name_cfg_template, 'connect cfg')
    local config = require('config')
    local instance_config = require('internal.config.instance_config')

    local connect_cfg = {
        name = replicaset_name,
        instances = {},
        reconnect_timeout = cfg.reconnect_timeout,
    }
    for _, instance_info in pairs(config:instances()) do
        if instance_info.replicaset_name == replicaset_name then
            local instance_name = instance_info.instance_name
            local iconfig = config:get('', {instance = instance_name})
            local endpoint = instance_config:instance_uri(iconfig, 'sharding')
            connect_cfg.instances[instance_name] = {endpoint = endpoint}
        end
    end
    if not next(connect_cfg.instances) then
        rs_error(box.error.REPLICASET_NOT_FOUND, replicaset_name)
    end
   return connect_by_cfg(connect_cfg)
end

-- Connect by given config or replicaset name an config.
local function connect_common(cfg_or_name, ...)
    if type(cfg_or_name) ~= 'string' and type(cfg_or_name) ~= 'table' then
        box.error(box.error.ILLEGAL_PARAMS, 'can connect by config (table) ' ..
                  'or name (string) but got ' .. type(cfg_or_name))
    end
    if type(cfg_or_name) == 'table' then
        return connect_by_cfg(cfg_or_name)
    else
        return connect_by_rs_name(cfg_or_name, ...)
    end
end

return {
    connect = connect_common,
}
