local fiber = require('fiber')
local clock = require('clock')
local config = require('config')
local checks = require('checks')
local fun = require('fun')
local netbox = require('net.box')

local WATCHER_DELAY = 0.1
local WATCHER_TIMEOUT = 10

local connections = {}

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

local connection_mode_update_cond = nil
local function connect(instance_name, opts)
    if not connection_mode_update_cond then
        connection_mode_update_cond = fiber.cond()
    end

    checks('string', {
        connect_timeout = '?number',
        wait_connected = '?boolean',
        fetch_schema = '?boolean',
    })
    opts = opts or {}

    local conn = connections[instance_name]
    if not is_connection_valid(conn, opts) then
        local uri = config:instance_uri('peer', {instance = instance_name})
        if uri == nil then
            local err = 'No suitable URI provided for instance %q'
            error(err:format(instance_name), 0)
        end

        local conn_opts = {
            connect_timeout = opts.connect_timeout,
            wait_connected = false,
            fetch_schema = opts.fetch_schema,
        }
        local ok, res = pcall(netbox.connect, uri, conn_opts)
        if not ok then
            local msg = 'Unable to connect to instance %q: %s'
            error(msg:format(instance_name, res.message), 0)
        end
        conn = res
        connections[instance_name] = conn
        local function mode(conn)
            if conn.state == 'active' then
                return conn._mode
            end
            return nil
        end
        conn.mode = mode
        local function watch_status(_key, value)
            conn._mode = value.is_ro and 'ro' or 'rw'
            connection_mode_update_cond:broadcast()
        end
        conn:watch('box.status', watch_status)
    end

    -- If opts.wait_connected is not false we wait until the connection is
    -- established or an error occurs (including a timeout error).
    if opts.wait_connected ~= false and conn:wait_connected() == false then
        local msg = 'Unable to connect to instance %q: %s'
        error(msg:format(instance_name, conn.error), 0)
    end
    return conn
end

local function is_candidate_connected(candidate)
    local conn = connections[candidate]
    return conn and conn.state == 'active' and conn:mode() ~= nil
end

-- Checks whether the candidate has responded with success or
-- with an error.
local function is_candidate_checked(candidate)
    local conn = connections[candidate]

    return not conn or
           is_candidate_connected(candidate) or
           conn.state == 'error' or
           conn.state == 'closed'
end

local function connect_to_candidates(candidates)
    local delay = WATCHER_DELAY
    local connect_deadline = clock.monotonic() + WATCHER_TIMEOUT

    for _, instance_name in pairs(candidates) do
        pcall(connect, instance_name, {
            wait_connected = false,
            connect_timeout = WATCHER_TIMEOUT
        })
    end

    assert(connection_mode_update_cond ~= nil)

    local connected_candidates = {}
    while clock.monotonic() < connect_deadline do
        connected_candidates = fun.iter(candidates)
            :filter(is_candidate_connected)
            :totable()

        local all_checked = fun.iter(candidates)
            :all(is_candidate_checked)

        if all_checked then
            return connected_candidates
        end

        connection_mode_update_cond:wait(delay)
    end
    return connected_candidates
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
    local conn = connections[instance_name]
    assert(conn ~= nil)
    return conn:mode() == mode
end

local function is_candidate_match_dynamic(instance_name, opts)
    assert(opts ~= nil and type(opts) == 'table')
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
    })
    opts = opts or {}
    if opts.mode ~= nil and opts.mode ~= 'ro' and opts.mode ~= 'rw' then
        local msg = 'Expected nil, "ro" or "rw", got "%s"'
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
    -- Return if retrieving dynamic information is not required.
    if next(static_candidates) == nil or next(dynamic_opts) == nil then
        return static_candidates
    end

    -- Filter the remaining candidates after connecting to them.
    local connected_candidates = connect_to_candidates(static_candidates)
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
    local candidates = filter(candidates_opts)
    if next(candidates) == nil then
        return nil, "no candidates are available with these conditions"
    end

    -- Initialize the weight of each candidate.
    local weights = {}
    if opts.mode == 'prefer_rw' or opts.mode == 'prefer_ro' then
        candidates = connect_to_candidates(candidates)
    end
    for _, instance_name in pairs(candidates) do
        weights[instance_name] = 0
    end

    -- Increase weights of candidates preferred by mode.
    if opts.mode == 'prefer_rw' or opts.mode == 'prefer_ro' then
        local mode = opts.mode == 'prefer_ro' and 'ro' or 'rw'
        local weight_mode = 2
        for _, instance_name in pairs(candidates) do
            local conn = connections[instance_name]
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
            local conn = connect(instance_name, {wait_connected = false})
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
}
