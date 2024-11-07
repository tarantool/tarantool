local t = require('luatest')
local config_builder = require('test.config-luatest.cbuilder')
local cluster_builder = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster_builder.init)
g.after_all(cluster_builder.clean)
g.after_each(cluster_builder.drop)

local SMALL_CLUSTER_SIZE = 2
local BIG_CLUSTER_SIZE = 3
local REDUNDANCY_FACTOR = 2

local function test_config(rs_count)
    local sharding_role = {
        privileges = {{permissions = {'execute'}, universe = true}},
    }
    local builder = config_builder.new()
    :set_global_option('credentials.roles.sharding', sharding_role)
    :set_global_option('credentials.users.storage.roles', {'sharding'})
    :set_global_option('credentials.users.storage.password', 'secret_storage')
    :set_global_option('iproto.advertise.sharding.login', 'storage')
    :use_group('test')
    builder:use_replicaset('router')
           :set_replicaset_option('roles', {'router'})
           :add_instance('router', {})
    for i = 1, rs_count do
        builder:use_replicaset('storage_' .. i)
               :set_replicaset_option('replication.failover', 'election')
               :set_replicaset_option('roles', {'storage'})

        for j = 1, REDUNDANCY_FACTOR do
            builder:add_instance('storage_' .. i .. '_' .. j, {})
        end
    end
    return builder:config()
end

local function start_small_cluster(cg)
    local config = test_config(SMALL_CLUSTER_SIZE)
    local cluster = cluster_builder.new(cg, config)
    local t = [[
        local f = function() end
        return {
            validate = f,
            apply = f,
            stop = f,
        }
    ]]
    local fio = require('fio')
    for _, name in pairs{'storage.lua', 'router.lua'} do
        local path = fio.pathjoin(cluster._dir, name)
        local f = fio.open(path, {'O_CREAT', 'O_WRONLY', 'O_TRUNC'})
        f:write(t)
        f:close()
    end
    cluster:start()
    return cluster
end

local function enlarge_cluster(cluster)
    local config = test_config(BIG_CLUSTER_SIZE)
    cluster:sync(config)

    local opts = {wait_until_ready = false}
    for i = SMALL_CLUSTER_SIZE + 1, BIG_CLUSTER_SIZE do
        for j = 1, REDUNDANCY_FACTOR do
            cluster['storage_' .. i .. '_' .. j]:start(opts)
        end
    end
    for i = SMALL_CLUSTER_SIZE + 1, BIG_CLUSTER_SIZE do
        for j = 1, REDUNDANCY_FACTOR do
            cluster['storage_' .. i .. '_' .. j]:wait_until_ready()
        end
    end

    cluster:reload(config)
end

-- Test method argument checks.
g.test_cluster_errors = function(cg)
    local cluster = start_small_cluster(cg)
    local router = cluster.router

    router:exec(function()
        local cluster = require('experimental.net.cluster')

        local msg = "Illegal parameters, option 'config_mode' of cluster cfg "
                .. "is 'magic' while must be 'auto' or 'manual' or nil"
        t.assert_error_msg_equals(msg, cluster.new, {config_mode = 'magic'})

        msg = "Illegal parameters, option 'connect_opts' of cluster cfg "
                .. "is true while must be table or nil"
        t.assert_error_msg_equals(msg, cluster.new, {connect_opts = true})

        msg = "Illegal parameters, unexpected option 'secret' in cluster cfg"
        t.assert_error_msg_equals(msg, cluster.new, {secret = true})

        local c1 = cluster.new{config_mode = 'auto'}
        msg = "Illegal parameters, Use cluster:reload_config(...) "
                .. "instead of cluster.reload_config(...)"
        t.assert_error_msg_equals(msg, c1.reload_config, {})

        msg = "Illegal parameters, manual reload expects manual config mode"
        t.assert_error_msg_equals(msg, c1.reload_config, c1, {})

        local c2 = cluster.new{config_mode = 'manual'}
        msg = "Illegal parameters, force reload expects auto config mode"
        t.assert_error_msg_equals(msg, c2.reload_config, c2, nil)

        local w = c1:watch_leaders('test', function() end)
        msg = "Illegal parameters, Use watcher:unregister(...) "
                .. "instead of watcher.unregister(...)"
        t.assert_error_msg_equals(msg, w.unregister)

        c1:close()
        c2:close()
        w:unregister()
    end)
end

-- Test cluster.connections.
g.test_cluster_connections = function(cg)
    local cluster = start_small_cluster(cg)
    local router = cluster.router

    -- Check connections and call each.
    local function check_connections(cluster_size)
        local fun = require('fun')
        local cluster = require('experimental.net.cluster')
        local opts = {roles = {'storage'}}

        t.helpers.retrying({timeout = 10}, function()
            t.assert_equals(fun.length(cluster.get_connections(opts)),
                    cluster_size)
            for i = 1, cluster_size do
                local shard_name = 'storage_' .. i
                local conn = cluster.get(shard_name)
                t.assert(conn)
                local info = conn:call_leader('box.info', {}, {timeout = 0.5})
                local inst_name = info.name
                t.assert(string.startswith(inst_name, shard_name))
            end
        end)
    end
    router:exec(check_connections, {SMALL_CLUSTER_SIZE})

    -- Reconfigure cluster.
    enlarge_cluster(cluster)

    -- Check connections and call each again.
    router:exec(check_connections, {BIG_CLUSTER_SIZE})
end

-- Test cluster.watch_leaders.
g.test_cluster_watcher = function(cg)
    local cluster = start_small_cluster(cg)
    local router = cluster.router
    router:exec(function()
        -- Set up a function that generates map: shard(s)_name -> given value.
        rawset(_G, 'shard_map', function(shard_count, value)
            local t = {}
            for i = 1, shard_count do
                t['storage_' .. i] = value
            end
            return t
        end)
    end)

    -- Initial broadcast.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function(alias)
                box.broadcast('test',
                        {alias = alias, value = box.info.ro and 0 or 1})
            end, {server.alias})
        end
    end)

    -- Test watcher when connections are not yet created.
    router:exec(function(cluster_size)
        local fun = require('fun')
        local cluster = require('experimental.net.cluster')
        local opts = {roles = {'storage'}}
        local shard_map = rawget(_G, 'shard_map')
        t.assert_equals(fun.length(cluster.get_connections(opts)), 0)

        local test_data = {
            by_alias = {},
            by_shard = {},
        }
        local function f(_, data, shard_name)
            if data then
                test_data.by_alias[data.alias] = data.value
                test_data.by_shard[shard_name] = data.value
            end
        end
        test_data.watcher = cluster.watch_leaders('test', f, opts)

        t.helpers.retrying({timeout = 10}, function()
            t.assert_equals(fun.length(test_data.by_alias), cluster_size)
            t.assert_equals(test_data.by_shard, shard_map(cluster_size, 1))
        end)
        rawset(_G, 'test_data1', test_data)
        -- Don't unregister this watcher.
    end, {SMALL_CLUSTER_SIZE})

    -- Test watcher when connections are already created.
    router:exec(function(cluster_size)
        local fun = require('fun')
        local cluster = require('experimental.net.cluster')
        local opts = {roles = {'storage'}}
        local shard_map = rawget(_G, 'shard_map')
        t.assert_equals(fun.length(cluster.get_connections(opts)), cluster_size)

        local test_data = {
            by_alias = {},
            by_shard = {},
        }
        local function f(_, data, shard_name)
            if data then
                test_data.by_alias[data.alias] = data.value
                test_data.by_shard[shard_name] = data.value
            end
        end
        test_data.watcher = cluster.watch_leaders('test', f, opts)

        t.helpers.retrying({timeout = 10}, function()
            t.assert_equals(fun.length(test_data.by_alias), cluster_size)
            t.assert_equals(test_data.by_shard, shard_map(cluster_size, 1))
        end)
        rawset(_G, 'test_data2', test_data)
        -- Unregister this watcher.
        test_data.watcher:unregister()
    end, {SMALL_CLUSTER_SIZE})

    -- Check that watcher function execution is not overlapped (per shard).
    router:exec(function(cluster_size)
        local fun = require('fun')
        local cluster = require('experimental.net.cluster')
        local opts = {roles = {'storage'}}
        local shard_map = rawget(_G, 'shard_map')
        t.assert_equals(fun.length(cluster.get_connections(opts)), cluster_size)

        local test_data = {
            enter_counts = {},
            exit_counts = {},
            blocked = {},
        }
        local function f(_, data, shard_name)
            if data then
                local c = test_data.enter_counts[shard_name] or 0
                test_data.enter_counts[shard_name] = c + 1
                c = test_data.exit_counts[shard_name] or 0
                test_data.exit_counts[shard_name] = c
                test_data.blocked[shard_name] = true
                local fiber = require('fiber')
                while test_data.blocked[shard_name] do
                    fiber.sleep(0.1)
                end
                test_data.exit_counts[shard_name] = c + 1
            end
        end
        test_data.watcher = cluster.watch_leaders('test', f, opts)

        t.helpers.retrying({timeout = 10}, function()
            t.assert_equals(test_data.enter_counts, shard_map(cluster_size, 1))
            t.assert_equals(test_data.exit_counts, shard_map(cluster_size, 0))
        end)
        rawset(_G, 'test_data3', test_data)
    end, {SMALL_CLUSTER_SIZE})

    -- Reconfigure - add one shard.
    enlarge_cluster(cluster)

    -- Broadcast again.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function(alias)
                box.broadcast('test',
                        {alias = alias, value = box.info.ro and 0 or 2})
            end, {server.alias})
        end
    end)

    -- Test that active watchers still work fine.
    router:exec(function(old_size, new_size)
        local shard_map = rawget(_G, 'shard_map')
        local test_data1 = rawget(_G, 'test_data1')
        local test_data2 = rawget(_G, 'test_data2')
        local test_data3 = rawget(_G, 'test_data3')
        -- This watcher eventually updates data.
        t.helpers.retrying({timeout = 10}, function()
            t.assert_equals(test_data1.by_shard, shard_map(new_size, 2))
        end)
        test_data1.watcher:unregister()

        -- This watcher is unregistered.
        t.assert_equals(test_data2.by_shard, shard_map(old_size, 1))

        -- This watcher is sleeping and don't update.
        t.assert_equals(test_data3.enter_counts, shard_map(new_size, 1))
        t.assert_equals(test_data3.exit_counts, shard_map(new_size, 0))

        -- Wake up the watcher and check eventual update.
        test_data3.blocked = shard_map(new_size, false)
        t.helpers.retrying({timeout = 10}, function()
            -- Note that the last shard don't call watcher function since
            -- the last time is was called with the latest updated value.
            t.assert_equals(test_data3.enter_counts,
                    {storage_1 = 2, storage_2 = 2, storage_3 = 1})
            t.assert_equals(test_data3.exit_counts, shard_map(new_size, 1))
        end)
        test_data3.watcher:unregister()
    end, {SMALL_CLUSTER_SIZE, BIG_CLUSTER_SIZE})
end

-- Test cluster.new.
g.test_cluster_new = function(cg)
    local cluster = start_small_cluster(cg)
    local router = cluster.router

    -- Initial broadcast.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                box.broadcast('test', 42)
            end)
        end
    end)

    -- Check auto mode.
    router:exec(function(cluster_size)
        local fun = require('fun')
        local cluster = require('experimental.net.cluster')
        local opts = {roles = {'storage'}}
        local shard_name = 'storage_1'

        local c = cluster.new{config_mode = 'auto'}
        t.assert_equals(fun.length(c:get_connections(opts)), 0)
        c:reload_config()
        t.assert_equals(fun.length(c:get_connections(opts)), cluster_size)
        t.helpers.retrying({timeout = 10}, function()
            local conn = c:get(shard_name)
            t.assert(conn)
            local info = conn:call_leader('box.info', {}, {timeout = 0.5})
            local inst_name = info.name
            t.assert(string.startswith(inst_name, shard_name))
        end)

        local val = 0
        c:watch_leaders('test', function() val = 42 end)
        t.helpers.retrying({timeout = 10}, function()
            t.assert_equals(val, 42)
        end)

        c:close()
    end, {SMALL_CLUSTER_SIZE})

    -- Check manual mode.
    router:exec(function()
        local fun = require('fun')
        local cluster = require('experimental.net.cluster')
        local shard_name = 'storage_1'

        local c = cluster.new{config_mode = 'manual'}
        t.assert_equals(fun.length(c:get_connections()), 0)
        c:reload_config({[shard_name] = {}})
        t.assert_equals(fun.length(c:get_connections()), 1)
        t.helpers.retrying({timeout = 10}, function()
            local conn = c:get(shard_name)
            t.assert(conn)
            local info = conn:call_leader('box.info', {}, {timeout = 0.5})
            local inst_name = info.name
            t.assert(string.startswith(inst_name, shard_name))
        end)

        local val = 0
        c:watch_leaders('test', function() val = 42 end)
        t.helpers.retrying({timeout = 10}, function()
            t.assert_equals(val, 42)
        end)

        c:close()
    end)
end

