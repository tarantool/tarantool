local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group()

local REDUNDANCY_FACTOR = 3

local function test_config()
    local sharding_role = {
        privileges = {{permissions = {'execute'}, universe = true}},
    }
    local builder = cbuilder:new()
    :set_global_option('credentials.roles.sharding', sharding_role)
    :set_global_option('credentials.users.storage.roles', {'sharding'})
    :set_global_option('credentials.users.storage.password', 'secret_storage')
    :set_global_option('credentials.users.backup.password', 'secret_backup')
    :set_global_option('iproto.advertise.sharding.login', 'storage')
    :use_group('test')
    builder:use_replicaset('router'):add_instance('router', {})
    builder:use_replicaset('storage')
           :set_replicaset_option('replication.failover', 'election')
    for i = 1, REDUNDANCY_FACTOR do
        builder:add_instance('storage' .. i, {})
    end
    return builder:config()
end

-- Get a valid storage instance name different from given.
local function get_another_storage_name(storage_name)
    local num = tonumber(string.sub(storage_name, 8))
    num = num + 1
    if num > REDUNDANCY_FACTOR then
        num = 1
    end
    return string.sub(storage_name, 1, 7) .. num
end

local function get_connect_cfg(cluster)
    local connect_cfg = {
        name = 'storage',
        instances = {},
    }
    for i = 1, REDUNDANCY_FACTOR do
        local instance_name = 'storage' .. i
        local uri = cluster[instance_name].net_box_uri
        local instance_config = {
            endpoint = {
                uri = uri,
                login = 'storage',
                password = 'secret_storage',
            }
        }
        connect_cfg.instances[instance_name] = instance_config
    end
    return connect_cfg
end

-- Test errors of replicaset connector.
g.test_net_replicaset_error = function()
    local config = test_config()
    config.groups.test.replicasets.storage.replication.failover = 'supervised'
    local cluster = cluster:new(config)
    cluster:start()
    local net_replicaset = require('internal.net.replicaset')
    local connect_cfg = get_connect_cfg(cluster)

    -- Check that unknown replicaset does not work.
    cluster.router:exec(function()
        local net_replicaset = require('internal.net.replicaset')
        local err_msg = 'The replicaset was not found by its name'
        local succ, err = pcall(net_replicaset.connect, 'moon')
        t.assert_equals(succ, false)
        t.assert_equals(err.message, err_msg)
        t.assert_equals(err.replicaset, 'moon')
    end)

    -- Preconnect to the replicaset.
    local rs = net_replicaset.connect(connect_cfg)
    local info = rs:call_leader('box.info')
    t.assert_equals(info.ro, false)

    -- Make all instances read-only.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                box.cfg{read_only = true}
            end)
        end
    end)

    -- Check that call_leader does not work without a leader.
    local err_msg = 'Writable instance was not found in replicaset'
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        local succ, err = pcall(rs.call_leader, rs, 'box.info',
                                {}, {timeout = 0.1})
        t.assert_equals(succ, false)
        t.assert_equals(err.message, err_msg)
        t.assert_equals(err.replicaset, 'storage')
    end)

    -- Reconnect and try that same.
    rs:close()
    rs = net_replicaset.connect(connect_cfg)
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        local succ, err = pcall(rs.call_leader, rs, 'box.info',
                                {}, {timeout = 0.1})
        t.assert_equals(succ, false)
        t.assert_equals(err.message, err_msg)
        t.assert_equals(err.replicaset, 'storage')
    end)

    -- Make all instances writable.
    config.groups.test.replicasets.storage.replication.failover = 'off'
    cluster:reload(config)
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                box.cfg{read_only = false}
            end)
        end
    end)

    -- Check that call_leader does not work without a leader.
    local err_msg = 'More than one writable was found in replicaset'
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        local succ, err = pcall(rs.call_leader, rs, 'box.info',
                                {}, {timeout = 0.1})
        t.assert_equals(succ, false)
        t.assert_equals(err.message, err_msg)
        t.assert_equals(err.replicaset, 'storage')
    end)
    -- Reconnect and try that same.
    rs:close()
    rs = net_replicaset.connect(connect_cfg)
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        local succ, err = pcall(rs.call_leader, rs, 'box.info',
                                {}, {timeout = 0.1})
        t.assert_equals(succ, false)
        t.assert_equals(err.message, err_msg)
        t.assert_equals(err.replicaset, 'storage')
    end)
    rs:close()
end

-- Test the normal work of replicaset connector.
g.test_net_replicaset_basics = function()
    local config = test_config()
    local cluster = cluster:new(config)
    cluster:start()
    local net_replicaset = require('internal.net.replicaset')
    local connect_cfg = get_connect_cfg(cluster)

    -- Connect using replicaset name.
    local leader_name = cluster.router:exec(function()
        local net_replicaset = require('internal.net.replicaset')
        local rs = net_replicaset.connect('storage')
        local info = rs:call_leader('box.info')
        t.assert_equals(info.ro, false)
        rs:close()
        return info.name
    end)

    -- Get config by replicaset name.
    cluster.router:exec(function(leader_name)
        local net_replicaset = require('internal.net.replicaset')
        local connect_cfg = net_replicaset.get_connect_cfg('storage')
        local rs = net_replicaset.connect(connect_cfg)
        local info = rs:call_leader('box.info')
        t.assert_equals(info.ro, false)
        t.assert_equals(info.name, leader_name)
        rs:close()
    end, {leader_name})

    -- Connect using config.
    local rs = net_replicaset.connect(connect_cfg)
    local info = rs:call_leader('box.info')
    t.assert_equals(info.ro, false)
    t.assert_equals(info.name, leader_name)

    -- Change the leader.
    local new_leader_name = get_another_storage_name(leader_name)
    cluster[new_leader_name]:exec(function()
        t.helpers.retrying({timeout = 10, delay = 0.1}, box.ctl.promote)
        box.ctl.wait_rw()
    end)

    -- Check rebind to the new leader.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        info = rs:call_leader('box.info')
        t.assert_equals(info.ro, false)
        t.assert_equals(info.name, new_leader_name)
    end)
end

-- Test that replicaset is deleted successfully by GC.
g.test_net_replicaset_gc = function()
    local config = test_config()
    local cluster = cluster:new(config)
    cluster:start()
    local net_replicaset = require('internal.net.replicaset')
    local connect_cfg = get_connect_cfg(cluster)

    local rs = net_replicaset.connect(connect_cfg)
    local info = rs:call_leader('box.info')
    t.assert_equals(info.ro, false)
    local status = {}
    local watcher = rs:watch_leader('box.status', function(_key, value)
        status = value
    end)
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(status.is_ro, false)
    end)

    local weak_ref = setmetatable({rs = rs}, {__mode = 'v'})
    -- Test also that replicaset internals also collected.
    for instance_name, instance in pairs(rs.instances) do
        weak_ref[instance_name] = instance
        weak_ref[instance_name .. '_conn'] = instance.conn
    end
    weak_ref.watcher = watcher

    watcher:unregister()
    watcher = nil
    rs = nil
    -- Calm down luacheck about unused values.
    t.assert(rs == nil)
    t.assert(watcher == nil)
    collectgarbage()
    -- Double collect to avoid possible resurrection effects.
    collectgarbage()
    -- Check reference to replicaset and watcher explicitly.
    t.assert(weak_ref.rs == nil)
    t.assert(weak_ref.watcher == nil)
    -- Check all other references too.
    for _, v in pairs(weak_ref) do
        t.assert(v == nil)
    end
end

-- Test watch_leader method.
g.test_watch_leader = function()
    local config = test_config()
    local cluster = cluster:new(config)
    cluster:start()
    local net_replicaset = require('internal.net.replicaset')
    local connect_cfg = get_connect_cfg(cluster)

    -- Wait for the leader.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        local leader_count = 0
        cluster:each(function(server)
            if server.alias:startswith('storage') then
                leader_count = leader_count + server:exec(function()
                    return box.info.ro and 0 or 1
                end)
            end
        end)
        t.assert_equals(leader_count, 1)
    end)

    -- Set initial value to event 'key1'.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                if not box.info.ro then
                    box.broadcast('key1', 1)
                else
                    box.broadcast('key1', 2)
                end
            end)
        end
    end)

    -- Subscribe and check that event was received only from leader.
    local rs = net_replicaset.connect(connect_cfg)
    local info = rs:call_leader('box.info')
    local leader_name = info.name
    local key1_value = 0
    local key2_value = 0
    local long_call_count = 0
    local long_wait = false

    local function func1(key, value, instance_name)
        t.assert_equals(key, 'key1')
        t.assert_equals(instance_name, leader_name)
        if value then
            key1_value = bit.bor(key1_value, value)
        end
    end
    local watcher1 = rs:watch_leader('key1', func1)

    local function func2(key, value, instance_name)
        t.assert_equals(key, 'key2')
        t.assert_equals(instance_name, leader_name)
        if value then
            key2_value = bit.bor(key2_value, value)
        end
    end
    local watcher2 = rs:watch_leader('key2', func2)

    local function long_func(_key, _value, instance_name)
        t.assert_equals(instance_name, leader_name)
        long_call_count = long_call_count + 1
        long_wait = true
        -- Don't leave until somebody resets long_wait.
        while long_wait do
            require('fiber').sleep(0.1)
        end
    end
    local long_watcher = rs:watch_leader('long', long_func)

    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(key1_value, 1)
    end)
    t.assert_equals(key2_value, 0)
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(long_call_count, 1)
    end)

    -- Set initial value to event 'key2' and update 'key1' and 'long'.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                if not box.info.ro then
                    box.broadcast('key1', 4)
                    box.broadcast('key2', 8)
                else
                    box.broadcast('key1', 16)
                    box.broadcast('key2', 32)
                end
                box.broadcast('long', math.random())
            end)
        end
    end)

    -- Check that events was received only from leader.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(key1_value, 1 + 4)
        t.assert_equals(key2_value, 8)
        t.assert_equals(long_call_count, 1)
    end)

    -- Change the leader.
    leader_name = get_another_storage_name(leader_name)
    cluster[leader_name]:exec(function()
        t.helpers.retrying({timeout = 10, delay = 0.1}, box.ctl.promote)
        box.ctl.wait_rw()
    end)

    -- Check that events was received after the leader change.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(key1_value, 1 + 4 + 16)
        t.assert_equals(key2_value, 8 + 32)
        t.assert_equals(long_call_count, 1)
    end)
    long_wait = false
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(long_call_count, 2)
    end)

    -- Set new values again.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                if not box.info.ro then
                    box.broadcast('key1', 64)
                    box.broadcast('key2', 128)
                else
                    box.broadcast('key1', 256)
                    box.broadcast('key2', 512)
                end
                box.broadcast('long', math.random())
            end)
        end
    end)

    -- Check that events was received from the leader.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(key1_value, 1 + 4 + 16 + 64)
        t.assert_equals(key2_value, 8 + 32 + 128)
        t.assert_equals(long_call_count, 2)
    end)
    long_wait = false
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(long_call_count, 3)
    end)

    -- Unregister watcher1 and check that there no more updates.
    watcher1:unregister()

    -- Set new values again.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                if not box.info.ro then
                    box.broadcast('key1', 1024)
                    box.broadcast('key2', 2048)
                else
                    box.broadcast('key1', 4096)
                    box.broadcast('key2', 8192)
                end
            end)
        end
    end)

    -- Check that events1 was not while event2 was received from the leader.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(key1_value, 1 + 4 + 16 + 64)
        t.assert_equals(key2_value, 8 + 32 + 128 + 2048)
    end)

    -- Change the leader again.
    leader_name = get_another_storage_name(leader_name)
    cluster[leader_name]:exec(function()
        t.helpers.retrying({timeout = 10, delay = 0.1}, box.ctl.promote)
        box.ctl.wait_rw()
    end)

    -- Check that events1 was not while event2 was received from the leader.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(key1_value, 1 + 4 + 16 + 64)
        t.assert_equals(key2_value, 8 + 32 + 128 + 2048 + 8192)
        t.assert_equals(long_call_count, 3)
    end)

    -- Set new values again.
    cluster:each(function(server)
        if server.alias:startswith('storage') then
            server:exec(function()
                if not box.info.ro then
                    box.broadcast('key1', 1024)
                    box.broadcast('key2', 16)
                else
                    box.broadcast('key1', 4096)
                    box.broadcast('key2', 64)
                end
            end)
        end
    end)

    -- Check that events1 was not while event2 was received from the leader.
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(key1_value, 1 + 4 + 16 + 64)
        t.assert_equals(key2_value, 8 + 32 + 128 + 2048 + 8192 + 16)
        t.assert_equals(long_call_count, 3)
    end)
    long_wait = false
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(long_call_count, 4)
    end)

    watcher1:unregister()
    watcher2:unregister()
    long_watcher:unregister()
    rs:close()
end

-- Test the login/password/params override threaded through instance_uri() and
-- forwarded by get_connect_cfg().
g.test_connection_override = function()
    local config = test_config()
    local cluster = cluster:new(config)
    cluster:start()

    cluster.router:exec(function()
        local config = require('config')
        local net_replicaset = require('internal.net.replicaset')

        -- No override: the sharding login/password come from the config.
        local uri = config:instance_uri('sharding', {instance = 'storage1'})
        t.assert_equals(uri.login, 'storage')
        t.assert_equals(uri.password, 'secret_storage')

        -- A login override re-resolves the password from the credentials.
        uri = config:instance_uri('sharding',
                                  {instance = 'storage1', login = 'backup'})
        t.assert_equals(uri.login, 'backup')
        t.assert_equals(uri.password, 'secret_backup')

        -- An explicit password is honored as-is.
        uri = config:instance_uri('sharding', {instance = 'storage1',
            login = 'backup', password = 'literal'})
        t.assert_equals(uri.login, 'backup')
        t.assert_equals(uri.password, 'literal')

        -- A params override is merged into the endpoint params.
        uri = config:instance_uri('sharding', {instance = 'storage1',
            params = {connect_timeout = 42}})
        t.assert_equals(uri.params.connect_timeout, 42)

        -- A login with no matching config user errors via find_password().
        t.assert_error_msg_contains('Cannot find user nobody in the config',
            function()
                config:instance_uri('sharding',
                                    {instance = 'storage1', login = 'nobody'})
            end)

        -- The override fields are type-checked.
        t.assert_error_msg_contains('Expected string, got number', function()
            config:instance_uri('sharding', {login = 5})
        end)
        t.assert_error_msg_contains('Expected string, got number', function()
            config:instance_uri('sharding', {password = 5})
        end)
        t.assert_error_msg_contains('Expected table, got number', function()
            config:instance_uri('sharding', {params = 5})
        end)

        -- A password without a login is rejected, not silently dropped.
        t.assert_error_msg_contains(
            'Password cannot be set without setting login', function()
                config:instance_uri('sharding', {instance = 'storage1',
                                                  password = 'literal'})
            end)

        -- get_connect_cfg() forwards the override to every instance endpoint.
        local cfg = net_replicaset.get_connect_cfg('storage',
            {login = 'backup', params = {connect_timeout = 7}})
        t.assert_not_equals(next(cfg.instances), nil)
        for _, instance in pairs(cfg.instances) do
            t.assert_equals(instance.endpoint.login, 'backup')
            t.assert_equals(instance.endpoint.password, 'secret_backup')
            t.assert_equals(instance.endpoint.params.connect_timeout, 7)
        end

        -- get_connect_cfg() rejects an unexpected override type.
        t.assert_error_msg_contains("option 'login' of opts is 'number'",
            function()
                net_replicaset.get_connect_cfg('storage', {login = 5})
            end)
    end)
end

-- Test the replicaset connection info.
g.test_net_replicaset_info = function()
    local config = test_config()
    local cluster = cluster:new(config)
    cluster:start()
    local net_replicaset = require('internal.net.replicaset')
    local rs = net_replicaset.connect(get_connect_cfg(cluster))

    -- Wait until the replicaset is healthy: a leader is seen and there are no
    -- alerts (every instance is reachable).
    t.helpers.retrying({timeout = 10, delay = 0.1}, function()
        t.assert_equals(next(rs:info().alerts), nil)
    end)

    local info = rs:info()
    t.assert_equals(info.replicaset, 'storage')
    t.assert_not_equals(info.leader, nil)
    t.assert_equals(info.alerts, {})
    -- Every instance is reported, and the leader is rw.
    local count = 0
    for _ in pairs(info.instances) do
        count = count + 1
    end
    t.assert_equals(count, REDUNDANCY_FACTOR)
    -- The leader is a healthy, connected instance with full per-instance info.
    local leader = info.instances[info.leader]
    t.assert_equals(leader.name, info.leader)
    t.assert_equals(leader.status, 'rw')
    t.assert_equals(leader.state, 'active')
    t.assert_equals(leader.error, nil)
    t.assert_type(leader.uri, 'string')
    -- The URI keeps the login but strips the password.
    t.assert_str_contains(leader.uri, 'storage@')
    t.assert_not_str_contains(leader.uri, 'secret_storage')
    t.assert_type(leader.uuid, 'string')

    rs:close()
end

-- Test the connection-health alerts reported by rs:info(). Each alert is
-- induced by connecting to a crafted set of endpoints; the cluster itself is
-- only observed.
g.test_net_replicaset_alerts = function()
    local config = test_config()
    local cluster = cluster:new(config)
    cluster:start()
    local net_replicaset = require('internal.net.replicaset')
    -- A unix socket nobody listens on: a connection to it stays 'unknown'.
    local unreachable = 'unix/:/tmp/net_replicaset_no_such.sock'

    local function connect(instances)
        local cfg = {name = 'storage', instances = {}}
        for name, uri in pairs(instances) do
            cfg.instances[name] = {endpoint = {
                uri = uri, login = 'storage', password = 'secret_storage'}}
        end
        return net_replicaset.connect(cfg)
    end

    -- Wait until rs:info() reports exactly the given alerts (matched by a
    -- substring each, in any order). The alerts are independent, so a case may
    -- expect more than one.
    local function wait_alerts(rs, ...)
        local needles = {...}
        t.helpers.retrying({timeout = 60}, function()
            local alerts = rs:info().alerts
            t.assert_equals(#alerts, #needles)
            local joined = ''
            for _, alert in ipairs(alerts) do
                joined = joined .. alert.message .. '\n'
            end
            for _, needle in ipairs(needles) do
                t.assert_str_contains(joined, needle)
            end
        end)
    end

    -- Find the current leader and its read-only followers.
    local leader_uri
    local follower_uris = {}
    t.helpers.retrying({timeout = 60}, function()
        leader_uri = nil
        follower_uris = {}
        cluster:each(function(server)
            if server.alias:startswith('storage') then
                if server:exec(function() return box.info.ro end) then
                    follower_uris[server.alias] = server.net_box_uri
                else
                    leader_uri = server.net_box_uri
                end
            end
        end)
        t.assert_not_equals(leader_uri, nil)
    end)

    -- The whole replicaset is unreachable: the only instance never connects, so
    -- it is both missing and leaderless.
    local rs = connect({bogus = unreachable})
    wait_alerts(rs, '1 instance(s) unreachable', 'has no writable leader')
    rs:close()

    -- No writable leader: only the read-only followers are reachable.
    t.assert_not_equals(next(follower_uris), nil)
    rs = connect(follower_uris)
    wait_alerts(rs, 'has no writable leader')
    rs:close()

    -- Split-brain: more than one writable leader. Two instance entries pointing
    -- at the same writable leader both advertise 'rw', so status_count.rw == 2.
    rs = connect({leader_a = leader_uri, leader_b = leader_uri})
    wait_alerts(rs, 'more than one writable leader')
    rs:close()

    -- A degraded but writable replicaset: the leader is reachable, one extra
    -- instance is not.
    rs = connect({leader = leader_uri, bogus = unreachable})
    wait_alerts(rs, '1 instance(s) unreachable')
    rs:close()

    -- The alerts are independent: a split-brained replicaset that is also
    -- missing an instance surfaces both problems at once, not just the first.
    rs = connect({leader_a = leader_uri, leader_b = leader_uri,
                  bogus = unreachable})
    wait_alerts(rs, 'more than one writable leader',
                '1 instance(s) unreachable')
    rs:close()
end
