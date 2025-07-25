local fiber = require('fiber')
local clock = require('clock')
local config = require('config')
local checks = require('checks')
local fun = require('fun')
local netbox = require('net.box')

local WATCHER_DELAY = 0.1
local WATCHER_TIMEOUT = 10

-- This option controls delay between reconnect attempts to
-- recently needed instances which connections have failed.
local RECONNECT_AFTER = 3

-- {{{ Basic instance connection pool

local pool_methods = {}
local pool_mt = {__index = pool_methods}

local function is_connection_valid(conn, opts)
    if conn == nil or conn.state == 'error' or conn.state == 'closed' then
        return false
    end
    assert(type(opts) == 'table')
    local conn_opts = conn.opts or {}
    if opts.fetch_schema ~= false and conn_opts.fetch_schema == false then
        return false
    end
    return true
end

function pool_methods._unused_connection_watchdog_step(self)
    local now = clock.monotonic()
    local until_next_deadline = math.huge
    local conns_to_close = {}

    for name, conn in pairs(self._connections) do
        local until_deadline = conn._deadline - now

        if until_deadline <= 0 then
            table.insert(conns_to_close, conn)
            self._connections[name] = nil
        elseif until_deadline < until_next_deadline then
            until_next_deadline = until_deadline
        end
    end

    for _, conn in ipairs(conns_to_close) do
        conn:close()
    end

    return until_next_deadline
end

function pool_methods._unused_connection_watchdog_loop(self)
    while true do
        local until_next_deadline = self:_unused_connection_watchdog_step()

        if until_next_deadline == math.huge then
            break
        end

        if not self._unused_connection_watchdog.fired then
            self._unused_connection_watchdog.cond:wait(until_next_deadline)
        end
        self._unused_connection_watchdog.fired = false
    end
end

function pool_methods._unused_connection_watchdog_wake(self)
    local f = self._unused_connection_watchdog_fiber
    if f ~= nil and f:status() ~= 'dead' then
        self._unused_connection_watchdog.fired = true
        self._unused_connection_watchdog.cond:signal()
        return
    end

    f = fiber.new(self._unused_connection_watchdog_loop, self)
    self._unused_connection_watchdog_fiber = f
end

function pool_methods._failed_connection_watchdog_step(self)
    local now = clock.monotonic()
    local until_next_reconnect = math.huge
    local instance_names_to_reconnect = {}

    -- At first, collect instances to reconnect and then
    -- perform actual reconnect not to modify self._connections
    -- in-place.
    for name, conn in pairs(self._connections) do
        if conn._reconnect ~= nil then
            local until_reconnect = conn._reconnect - now

            if until_reconnect <= 0 then
                table.insert(instance_names_to_reconnect, name)
            elseif until_reconnect < until_next_reconnect then
                until_next_reconnect = until_reconnect
            end
        end
    end

    for _, instance_name in ipairs(instance_names_to_reconnect) do
        local old_conn = self._connections[instance_name]

        -- The connection has not been nil when collected and
        -- the function did not yield. It should remain non-nil.
        -- In other words, if the connection has not been
        -- closed as unused it remains so during the whole step.
        --
        -- This non-yielding logic prevents possible races
        -- when reconnect happens at the same time when deadline
        -- is reached.
        assert(old_conn ~= nil)

        local opts = {
            ttl = 0,
            wait_connected = false,
            fetch_schema = old_conn.opts.fetch_schema,
        }

        if not is_connection_valid(old_conn, opts) then
            local new_conn = self:connect(instance_name, opts)
            new_conn._reconnect = now + RECONNECT_AFTER
            if until_next_reconnect > RECONNECT_AFTER then
                until_next_reconnect = RECONNECT_AFTER
            end
        else
            old_conn._reconnect = nil
        end
    end

    return until_next_reconnect
end

function pool_methods._failed_connection_watchdog_loop(self)
    while true do
        local until_next_reconnect = self:_failed_connection_watchdog_step()

        if until_next_reconnect == math.huge then
            break
        end

        if not self._failed_connection_watchdog.fired then
            self._failed_connection_watchdog.cond:wait(until_next_reconnect)
        end
        self._failed_connection_watchdog.fired = false
    end
end

function pool_methods._failed_connection_watchdog_wake(self)
    local f = self._failed_connection_watchdog_fiber
    if f ~= nil and f:status() ~= 'dead' then
        self._failed_connection_watchdog.fired = true
        self._failed_connection_watchdog.cond:signal()
        return
    end

    f = fiber.new(self._failed_connection_watchdog_loop, self)
    self._failed_connection_watchdog_fiber = f
end

--- Get a cached connection to instance. Might be nil.
function pool_methods.get_connection(self, instance_name)
    return self._connections[instance_name]
end

--- Connect to an instance or receive a cached connection by
--- name.
---
--- The method returns a connection as the first return value
--- and an error message as the second one.
function pool_methods.connect(self, instance_name, opts)
    checks('table', 'string', {
        ttl = '?number',
        connect_timeout = '?number',
        wait_connected = '?boolean',
        fetch_schema = '?boolean',
    })
    opts = opts or {}

    local conn = self._connections[instance_name]
    local old_deadline = (conn or {})._deadline
    local old_reconnect = (conn or {})._reconnect
    if not is_connection_valid(conn, opts) then
        local uri = config:instance_uri('peer', {instance = instance_name})
        if uri == nil then
            local err = 'No suitable URI provided for instance %q'
            return nil, err:format(instance_name)
        end

        local conn_opts = {
            connect_timeout = opts.connect_timeout,
            wait_connected = false,
            fetch_schema = opts.fetch_schema,
        }
        local ok, res = pcall(netbox.connect, uri, conn_opts)
        if not ok then
            local msg = 'Unable to connect to instance %q: %s'
            return nil, msg:format(instance_name, res.message)
        end
        conn = res
        self._connections[instance_name] = conn
        local function mode(conn)
            if conn.state == 'active' then
                return conn._mode
            end
            return nil
        end
        conn.mode = mode
        conn._deadline = old_deadline
        conn._reconnect = old_reconnect
        local function watch_status(_key, value)
            conn._mode = value.is_ro and 'ro' or 'rw'
            self._connection_mode_update_cond:broadcast()
        end
        conn:watch('box.status', watch_status)
        local function on_disconnect()
            conn._reconnect = clock.monotonic() + RECONNECT_AFTER
            self:_failed_connection_watchdog_wake()
        end
        conn:on_disconnect(on_disconnect)
    end

    local idle_timeout = opts.ttl or self._idle_timeout
    local new_deadline = clock.monotonic() + idle_timeout

    if old_deadline == nil or new_deadline > old_deadline then
        conn._deadline = new_deadline
        self:_unused_connection_watchdog_wake()
    end

    -- If opts.wait_connected is not false we wait until the connection is
    -- established or an error occurs (including a timeout error).
    if opts.wait_connected ~= false and conn:wait_connected() == false then
        local msg = 'Unable to connect to instance %q: %s'
        return nil, msg:format(instance_name, conn.error)
    end
    return conn
end

--- This method connects to the specified instances and returns
--- the set of successfully connected ones. Instances that are
--- known to have been recently unavailable are skipped.
---
--- If a callback accepting an instance name and returning a
--- boolean value is provided in `opts.any` the method connects
--- to multiple instances and try to find one satisfying the
--- callback. It lasts until all of the instances are known to
--- be unsuitable (either failed or not satisfying the callback)
--- or the timeout is reached.
function pool_methods.connect_to_multiple(self, instances, opts)
    checks('table', 'table', {
        any = '?function',
        timeout = '?number',
    })

    if opts == nil then opts = {} end
    if next(instances) == nil then return {} end
    assert(opts.any == nil or type(opts.any) == 'function')

    -- Checks whether the candidate is connected and active.
    local function is_instance_connected(instance_name)
        local conn = self._connections[instance_name]
        return conn and conn.state == 'active' and conn:mode() ~= nil
    end

    -- Checks whether the candidate has responded with success or
    -- with an error or connection to it isn't established.
    local function is_instance_checked(instance_name)
        local conn = self._connections[instance_name]
        if conn == nil then
            return true
        end

        return is_instance_connected(instance_name) or
               conn.state == 'error' or conn.state == 'closed'
    end

    local delay = WATCHER_DELAY
    local timeout = opts.timeout or WATCHER_TIMEOUT
    local connect_deadline = clock.monotonic() + timeout

    -- We divide instances into three categories.
    -- * Available ones: these instances are already connected.
    --   Nothing to do with them apart from returning them.
    -- * Failed: the pool assumes an instance failed if it has
    --   been recently accessed but the connection has failed.
    --   Such instances are not returned and not waited. The pool
    --   tries to automatically reconnect to them in the
    --   background guaranteeing the failed status is relatively
    --   actual.
    -- * Unknown: these are either or the ones that have been
    --   flushed because they have not been needed for a while
    --   (see idle timeout and related logic) or the ones not
    --   accessed since startup. The pool connects to them and
    --   waits for success/fail.
    local candidate_instances = {}
    for _, instance_name in pairs(instances) do
        local conn = self._connections[instance_name]

        -- If instance has not been accessed recently (it has
        -- unknown state) start connecting to it. The call assume
        -- it is a candidate and will wait for the connection to
        -- fail/succeed.
        if conn == nil then
            self:connect(instance_name, {wait_connected = false})
            table.insert(candidate_instances, instance_name)
        -- If the connection is already ok it is likely it should
        -- be returned as is.
        elseif is_connection_valid(conn,
            {fetch_schema = (conn.opts or {}).fetch_schema}) then
            table.insert(candidate_instances, instance_name)
        -- The remaining connections are the failed ones that has
        -- been checked rather recently. Skip them.
        end
    end

    local connected_instances = {}
    while clock.monotonic() < connect_deadline do
        connected_instances = fun.iter(candidate_instances)
            :filter(is_instance_connected)
            :totable()

        if opts.any then
            if fun.iter(candidate_instances):any(opts.any) then
                break
            end
        end

        if fun.iter(candidate_instances):all(is_instance_checked) then
            break
        end

        self._connection_mode_update_cond:wait(delay)
    end
    return connected_instances
end

--- Set the default time in which instances now used within
--- the `connect()` method would be closed.
---
--- Note that specifying a new timeout does not cause
--- recalculation of the existing deadlines.
function pool_methods.set_idle_timeout(self, idle_timeout)
    self._idle_timeout = idle_timeout
end

local function create_pool()
    return setmetatable({
        _connections = {},
        _connection_mode_update_cond = fiber.cond(),

        -- Unused connection management
        _unused_connection_watchdog_fiber = nil,
        _unused_connection_watchdog = {
            fired = false,
            cond = fiber.cond(),
        },

        -- Failed connection management
        _failed_connection_watchdog_fiber = nil,
        _failed_connection_watchdog = {
            fired = false,
            cond = fiber.cond(),
        },

        _idle_timeout = 60,
    }, pool_mt)
end

-- }}} Basic instance connection pool

local pool = create_pool()

local function set_idle_timeout(idle_timeout)
    pool:set_idle_timeout(idle_timeout)
end

local function connect(instance_name, opts)
    checks('string', {
        connect_timeout = '?number',
        wait_connected = '?boolean',
        fetch_schema = '?boolean',
    })

    opts = opts and table.copy(opts) or {}
    opts.ttl = math.huge

    local conn, err  = pool:connect(instance_name, opts)
    if err ~= nil then
        error(err, 0)
    end

    return conn
end

local function is_group_match(expected_groups, present_group)
    if expected_groups == nil or next(expected_groups) == nil then
        return true
    end
    for _, group in pairs(expected_groups) do
        if group == present_group then
            return true
        end
    end
    return false
end

local function is_replicaset_match(expected_replicasets, present_replicaset)
    if expected_replicasets == nil or next(expected_replicasets) == nil then
        return true
    end
    for _, replicaset in pairs(expected_replicasets) do
        if replicaset == present_replicaset then
            return true
        end
    end
    return false
end

local function is_instance_match(expected_instances, present_instance)
    if expected_instances == nil or next(expected_instances) == nil then
        return true
    end
    for _, instance in pairs(expected_instances) do
        if instance == present_instance then
            return true
        end
    end
    return false
end

local function is_roles_match(expected_roles, present_roles)
    if expected_roles == nil or next(expected_roles) == nil then
        return true
    end
    if present_roles == nil or next(present_roles) == nil then
        return false
    end

    local roles = {}
    for _, present_role_name in pairs(present_roles) do
        roles[present_role_name] = true
    end
    for _, expected_role_name in pairs(expected_roles) do
        if roles[expected_role_name] == nil then
            return false
        end
    end
    return true
end

local function is_labels_match(expected_labels, present_labels)
    if expected_labels == nil or next(expected_labels) == nil then
        return true
    end
    if present_labels == nil or next(present_labels) == nil then
        return false
    end

    for label, value in pairs(expected_labels) do
        if present_labels[label] ~= value then
            return false
        end
    end
    return true
end

local function is_candidate_match_static(names, opts)
    assert(opts ~= nil and type(opts) == 'table')
    local get_opts = {instance = names.instance_name}
    return is_group_match(opts.groups, names.group_name) and
           is_replicaset_match(opts.replicasets, names.replicaset_name) and
           is_instance_match(opts.instances, names.instance_name) and
           is_roles_match(opts.roles, config:get('roles', get_opts)) and
           is_roles_match(opts.sharding_roles,
                          config:get('sharding.roles', get_opts)) and
           is_labels_match(opts.labels, config:get('labels', get_opts))
end

local function is_mode_match(mode, instance_name)
    if mode == nil then
        return true
    end
    local conn = pool:get_connection(instance_name)
    assert(conn ~= nil and conn:mode() ~= nil)
    return conn:mode() == mode
end

local function is_candidate_match_dynamic(instance_name, opts)
    assert(opts ~= nil and type(opts) == 'table')

    local conn = pool:get_connection(instance_name)
    if not conn or conn:mode() == nil then
        return
    end

    return is_mode_match(opts.mode, instance_name)
end

local function filter(opts)
    checks({
        groups = '?table',
        replicasets = '?table',
        instances = '?table',
        labels = '?table',
        roles = '?table',
        sharding_roles = '?table',
        mode = '?string',
        skip_connection_check = '?boolean',
        -- Internal option on how filter() behaves.
        -- Used internally in the call() method.
        --
        -- The value is represented by enum (the values are sorted
        -- from the fastest to the slowest):
        -- * `no wait` -- only return already polled instances.
        -- * `any` -- wait for any instance satisfying requirements
        --   to be polled.
        -- * `wait` (default) -- wait for all instances to be
        --   polled.
        _wait_mode = '?string',
    })
    opts = opts or {}

    local wait_mode = opts._wait_mode
    if wait_mode == nil then
        wait_mode = 'wait'
    end

    if opts.mode ~= nil and opts.mode ~= 'ro' and opts.mode ~= 'rw' then
        local msg = 'Expected nil, "ro" or "rw", got "%s"'
        error(msg:format(opts.mode), 0)
    end

    if opts.skip_connection_check and opts.mode ~= nil then
        local msg = 'Filtering by mode "%s" requires the connection ' ..
                    'check but it\'s been disabled by the ' ..
                    '"skip_connection_check" option'
        error(msg:format(opts.mode), 0)
    end

    if opts.sharding_roles ~= nil then
        for _, sharding_role in ipairs(opts.sharding_roles) do
            if sharding_role == 'rebalancer' then
               error('Filtering by the \"rebalancer\" role is not supported',
                     0)
            elseif sharding_role ~= 'storage' and
               sharding_role ~= 'router' then
               local msg = 'Unknown sharding role \"%s\" in '..
                           'connpool.filter() call. Expected one of the '..
                           '\"storage\", \"router\"'
               error(msg:format(sharding_role), 0)
            end
        end
    end
    local static_opts = {
        groups = opts.groups,
        replicasets = opts.replicasets,
        instances = opts.instances,
        labels = opts.labels,
        roles = opts.roles,
        sharding_roles = opts.sharding_roles
    }
    local dynamic_opts = {
        mode = opts.mode,
    }

    -- First, select candidates using the information from the config.
    local static_candidates = {}
    for instance_name, names in pairs(config:instances()) do
        if is_candidate_match_static(names, static_opts) then
            table.insert(static_candidates, instance_name)
        end
    end

    -- Return if the connection check isn't needed.
    if opts.skip_connection_check then
        return static_candidates
    end

    local connected_candidates
    if wait_mode == 'wait' then
        -- Filter the remaining candidates after connecting to them.
        --
        -- The connect_to_candidates() call returns quickly if it
        -- receives empty table as an argument.
        connected_candidates = pool:connect_to_multiple(static_candidates)
    elseif wait_mode == 'any' then
        -- Try to connect to the candidates until at least one
        -- satisfying the dynamic requirements is found.
        connected_candidates = pool:connect_to_multiple(static_candidates, {
            any = function(instance_name)
                return is_candidate_match_dynamic(instance_name, dynamic_opts)
            end,
        })
    elseif wait_mode == 'no wait' then
        -- Only continue with already polled instances.
        connected_candidates = static_candidates
    else
        assert(false, 'Unexpected _wait_mode value.')
    end

    local dynamic_candidates = {}
    for _, instance_name in pairs(connected_candidates) do
        if is_candidate_match_dynamic(instance_name, dynamic_opts) then
            table.insert(dynamic_candidates, instance_name)
        end
    end
    return dynamic_candidates
end

local function get_connection(opts)
    local mode = nil
    if opts.mode == 'ro' or opts.mode == 'rw' then
        mode = opts.mode
    end
    local candidates_opts = {
        groups = opts.groups,
        replicasets = opts.replicasets,
        instances = opts.instances,
        labels = opts.labels,
        roles = opts.roles,
        sharding_roles = opts.sharding_roles,
        mode = mode,
    }

    -- If the mode is not prefer_*, try to find already connected
    -- candidates.
    local candidates
    if opts.mode == 'prefer_rw' or opts.mode == 'prefer_ro' then
        candidates = filter(candidates_opts)
    else
        -- Try to find already connected candidates.
        candidates_opts._wait_mode = 'no wait'
        candidates = filter(candidates_opts)

        -- Try to find at least one connected otherwise.
        if next(candidates) == nil then
            candidates_opts._wait_mode = 'any'
            candidates = filter(candidates_opts)
        end

        -- Last resort: try to connect to all the candidates.
        if next(candidates) == nil then
            candidates_opts._wait_mode = 'wait'
            candidates = filter(candidates_opts)
        end
    end

    if next(candidates) == nil then
        return nil, "no candidates are available with these conditions"
    end

    -- Initialize the weight of each candidate.
    local weights = {}
    for _, instance_name in pairs(candidates) do
        weights[instance_name] = 0
    end

    -- Increase weights of candidates preferred by mode.
    if opts.mode == 'prefer_rw' or opts.mode == 'prefer_ro' then
        local mode = opts.mode == 'prefer_ro' and 'ro' or 'rw'
        local weight_mode = 2
        for _, instance_name in pairs(candidates) do
            local conn = pool:get_connection(instance_name)
            assert(conn ~= nil)
            if conn:mode() == mode then
                weights[instance_name] = weights[instance_name] + weight_mode
            end
        end
    end

    -- Increase weight of local candidate.
    if opts.prefer_local ~= false then
        local weight_local = 1
        if weights[box.info.name] ~= nil then
            weights[box.info.name] = weights[box.info.name] + weight_local
        end
    end

    -- Select candidate by weight.
    while next(weights) ~= nil do
        local max_weight = 0
        for _, weight in pairs(weights) do
            if weight > max_weight then
                max_weight = weight
            end
        end
        local preferred_candidates = {}
        for instance_name, weight in pairs(weights) do
            if weight == max_weight then
                table.insert(preferred_candidates, instance_name)
            end
        end
        while #preferred_candidates > 0 do
            local n = math.random(#preferred_candidates)
            local instance_name = table.remove(preferred_candidates, n)
            local conn = pool:get_connection(instance_name)
            if conn:wait_connected() then
                return conn
            end
        end
        for _, instance_name in pairs(preferred_candidates) do
            weights[instance_name] = nil
        end
    end
    return nil, "connection to candidates failed"
end

local function call(func_name, args, opts)
    checks('string', '?table', {
        groups = '?table',
        replicasets = '?table',
        instances = '?table',
        labels = '?table',
        roles = '?table',
        sharding_roles = '?table',
        prefer_local = '?boolean',
        mode = '?string',
        -- The following options passed directly to net.box.call().
        timeout = '?',
        buffer = '?',
        on_push = '?function',
        on_push_ctx = '?',
        is_async = '?boolean',
    })
    opts = opts or {}
    if opts.mode ~= nil and opts.mode ~= 'ro' and opts.mode ~= 'rw' and
       opts.mode ~= 'prefer_ro' and opts.mode ~= 'prefer_rw' then
        local msg = 'Expected nil, "ro", "rw", "prefer_ro" or "prefer_rw", ' ..
                    'got "%s"'
        error(msg:format(opts.mode), 0)
    end

    local conn_opts = {
        groups = opts.groups,
        replicasets = opts.replicasets,
        instances = opts.instances,
        labels = opts.labels,
        roles = opts.roles,
        sharding_roles = opts.sharding_roles,
        prefer_local = opts.prefer_local,
        mode = opts.mode,
    }
    local conn, err = get_connection(conn_opts)
    if conn == nil then
        local msg = "Couldn't execute function %s: %s"
        error(msg:format(func_name, err), 0)
    end

    local net_box_call_opts = {
        timeout = opts.timeout,
        buffer = opts.buffer,
        on_push = opts.on_push,
        on_push_ctx = opts.on_push_ctx,
        is_async = opts.is_async,
    }
    return conn:call(func_name, args, net_box_call_opts)
end

return {
    connect = connect,
    filter = filter,
    call = call,

    set_idle_timeout = set_idle_timeout,
}
