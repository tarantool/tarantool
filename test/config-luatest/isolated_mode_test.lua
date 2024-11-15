local yaml = require('yaml')
local net_box = require('net.box')
local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')
local cluster = require('test.config-luatest.cluster')
local it = require('test.interactive_tarantool')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

g.after_each(function(g)
    if g.it ~= nil then
        g.it:close()
    end
    if g.conn ~= nil then
        g.conn:close()
    end
end)

-- Connect to the given server's console and wait for "ready"
-- configuration status.
--
-- Save to g.it to let the after_each hook close the connection in
-- case of an error.
local function connect_console(g, server)
    t.helpers.retrying({timeout = 60}, function()
        g.it = it.connect(server)
    end)

    -- The same check as <luatest.server>:start() performs, but
    -- using the console connection.
    t.helpers.retrying({timeout = 60}, function()
        local status = g.it:roundtrip('box.info.config.status')
        assert(status == 'ready' or status == 'check_warnings')
    end)
end

-- Verify that an instance can't start in the isolated mode if
-- there is no local snapshot.
g.test_startup_no_snap = function()
    local config = cbuilder:new()
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'i-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {isolated = true})
        :config()

    -- Write config to a temporary directory.
    local dir = treegen.prepare_directory({}, {})
    local config_file = treegen.write_file(dir, 'config.yaml',
        yaml.encode(config))

    -- Run tarantool instance that is expected to exit
    -- immediately.
    local env = {}
    local args = {'--name', 'i-003', '--config', config_file}
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, env, args, opts)

    -- Verify the exit code and the error reported to stderr.
    local exp_err = 'Startup failure.\n' ..
        'The isolated mode is enabled and the instance "i-003" has no local ' ..
        'snapshot. An attempt to bootstrap the instance would lead to the ' ..
        'split-brain situation.'
    t.assert_covers(res, {
        exit_code = 1,
        stderr = ('LuajitError: %s\nfatal error, exiting the event loop')
            :format(exp_err),
    })
end

-- The opposite to the previous test case: verify that an instance
-- can start in the isolated mode if there is a local snapshot.
g.test_startup_with_snap = function(g)
    local config = cbuilder:new()
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'i-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Stop i-003. It leaves a local snapshot.
    cluster['i-003']:stop()

    -- Mark i-003 as isolated in the configuration, write it to
    -- the file.
    local config_2 = cbuilder:new(config)
        :set_instance_option('i-003', 'isolated', true)
        :config()
    cluster:sync(config_2)

    -- Start the instance again from the local snapshot in the
    -- isolated mode.
    --
    -- Don't check the readiness condition, because it is
    -- performed over an iproto connection and it has no chance to
    -- succeed (iproto stops listening in the isolated mode).
    cluster['i-003']:start({wait_until_ready = false})

    -- Use the console connection, because an instance in the
    -- isolated mode doesn't accept iproto requests.
    connect_console(g, cluster['i-003'])

    -- Verify that the instance is started in the isolated mode.
    g.it:roundtrip("require('config'):get('isolated')", true)

    -- Verify that the instance loaded the database.
    g.it:roundtrip("box.space._schema:get({'replicaset_name'})[2]",
        'replicaset-001')
end

-- Verify that the instance goes to RO in the isolated mode
-- disregarding of any other configuration options.
--
-- The test case verifies that it occurs on a runtime
-- reconfiguration as well as on a startup.
g.test_read_only = function(g)
    local function assert_isolated(exp)
        g.it:roundtrip("require('config'):get('isolated')", exp)
    end

    local function assert_read_only(exp)
        g.it:roundtrip('box.info.ro', exp)
    end

    local config = cbuilder:new()
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'i-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Use the console connection, because an instance in the
    -- isolated mode doesn't accept iproto requests.
    g.it = it.connect(cluster['i-001'])

    -- Mark i-001 as isolated, reload the configuration.
    local config_2 = cbuilder:new(config)
        :set_instance_option('i-001', 'isolated', true)
        :config()
    cluster:sync(config_2)
    g.it:roundtrip("require('config'):reload()")

    -- The isolated mode is applied, the instance goes to RO.
    assert_isolated(true)
    assert_read_only(true)

    -- Restart i-001, reconnect the console.
    --
    -- Don't check the readiness condition, because it is
    -- performed over an iproto connection and it has no chance to
    -- succeed (iproto stops listening in the isolated mode).
    g.it:close()
    cluster['i-001']:stop()
    cluster['i-001']:start({wait_until_ready = false})
    connect_console(g, cluster['i-001'])

    -- Still in the isolated mode and RO.
    assert_isolated(true)
    assert_read_only(true)

    -- Disable the isolated mode on i-001.
    local config_3 = cbuilder:new(config_2)
        :set_instance_option('i-001', 'isolated', nil)
        :config()
    cluster:sync(config_3)
    g.it:roundtrip("require('config'):reload()")

    -- Goes to RW.
    --
    -- Replication upstreams reconfiguration may let the instance
    -- go to the orphan status for a short time. The orphan status
    -- means box.info.ro = true even if the instance is configured
    -- as RW. Retry the RW check to make the test case stable.
    assert_isolated(false)
    t.helpers.retrying({timeout = 60}, function()
        assert_read_only(false)
    end)
end

-- Verify that an instance in the isolated mode stops listening
-- for new iproto connections and drops existing connections.
g.test_iproto_stop = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Connect to the server's console and verify that it works.
    g.it = it.connect(cluster['i-001'])
    g.it:roundtrip('42', 42)

    -- Connect to the server using iproto and verify that it
    -- works.
    local uri = cluster['i-001'].net_box_uri
    g.conn = net_box.connect(uri)
    t.assert(g.conn:ping())

    -- Go to the isolated mode.
    local config_2 = cbuilder:new(config)
        :set_instance_option('i-001', 'isolated', true)
        :config()
    cluster:sync(config_2)
    g.it:roundtrip("require('config'):reload()")

    -- The isolated mode is applied.
    g.it:roundtrip("require('config'):get('isolated')", true)

    -- The existing connection is dropped.
    t.assert_not(g.conn:ping())

    -- A new connection is not accepted.
    g.conn:close()
    g.conn = net_box.connect(uri)
    t.assert_equals(g.conn.state, 'error')
    t.assert_not(g.conn:ping())
end

-- Similar to the previous one, but imitates a timeout error on
-- waiting for iproto connections drop.
--
-- The idea of the test case is to verify that the alert is issued
-- in the case.
g.test_iproto_stop_failure = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Imitate a timeout error on dropping iproto connections.
    cluster['i-001']:exec(function()
        os.setenv('TT_CONFIG_DROP_CONNECTION_TIMEOUT', '0')
    end)

    -- Connect to the server's console and verify that it works.
    g.it = it.connect(cluster['i-001'])
    g.it:roundtrip('42', 42)

    -- Connect to the server using iproto and verify that it
    -- works.
    local uri = cluster['i-001'].net_box_uri
    g.conn = net_box.connect(uri)
    t.assert(g.conn:ping())

    -- Go to the isolated mode.
    local config_2 = cbuilder:new(config)
        :set_instance_option('i-001', 'isolated', true)
        :config()
    cluster:sync(config_2)
    g.it:roundtrip("require('config'):reload()")

    -- The isolated mode is applied.
    g.it:roundtrip("require('config'):get('isolated')", true)

    -- Verify that an alert is issued.
    --
    -- The drop connection alert is set in background, so let's
    -- retry the check until the alert is found.
    local info
    local exp_1 = 'The isolated mode is set for the instance "i-001"'
    local exp_2 = 'isolated mode: can\'t drop iproto connections during 0 ' ..
        'seconds (continued in background): timed out'
    t.helpers.retrying({timeout = 60}, function()
        info = g.it:roundtrip("require('config'):info()")
        t.assert_covers(info, {
            alerts = {
                {type = 'warn', message = exp_1},
                {type = 'warn', message = exp_2},
            },
        })
    end)

    -- Only one drop connection alert, no duplicates or extra
    -- warnings.
    t.assert_equals(#info.alerts, 2)

    -- The existing connection is dropped despite the alert.
    t.assert_not(g.conn:ping())

    -- A new connection is not accepted despite the alert.
    g.conn:close()
    g.conn = net_box.connect(uri)
    t.assert_equals(g.conn.state, 'error')
    t.assert_not(g.conn:ping())
end

-- Similar to the previous one, but imitates a timeout error on
-- waiting for iproto connections drop on startup.
--
-- The test case mostly to verify that the alert is not
-- duplicated.
g.test_iproto_stop_failure_on_startup = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :config()

    local cluster = cluster.new(g, config, {
        env = {
            -- Imitate a timeout error on dropping iproto
            -- connections.
            ['TT_CONFIG_DROP_CONNECTION_TIMEOUT'] = '0',
        },
    })
    cluster:start()

    -- Enable the isolated mode, write the new config.
    local config_2 = cbuilder:new(config)
        :set_instance_option('i-001', 'isolated', true)
        :config()
    cluster:sync(config_2)

    -- If there are no connections to the instance, it is possible
    -- that the alert is not issued even with zero timeout.
    --
    -- It is OK. Just retry the startup in the case to attempt to
    -- trigger the alert again.
    local info
    t.helpers.retrying({timeout = 60}, function()
        -- Stop i-001. It leaves a local snapshot, so it can be
        -- started in the isolated mode.
        cluster['i-001']:stop()

        -- Start the instance from the local snapshot in the
        -- isolated mode.
        --
        -- Don't check the readiness condition, because it is
        -- performed over an iproto connection and it has no
        -- chance to succeed (iproto stops listening in the
        -- isolated mode).
        cluster['i-001']:start({wait_until_ready = false})

        -- Use the console connection, because an instance in the
        -- isolated mode doesn't accept iproto requests.
        connect_console(g, cluster['i-001'])

        -- The isolated mode is applied.
        g.it:roundtrip("require('config'):get('isolated')", true)

        -- Verify that an alert is issued.
        --
        -- The drop connection alert is set in background, so
        -- let's retry the check until the alert is found or a
        -- short timeout exceeds (in this case the outer
        -- retrying logic will catch it).
        local exp_1 = 'The isolated mode is set for the instance "i-001"'
        local exp_2 = 'isolated mode: can\'t drop iproto connections during ' ..
            '0 seconds (continued in background): timed out'
        t.helpers.retrying({timeout = 1}, function()
            info = g.it:roundtrip("require('config'):info()")
            t.assert_covers(info, {
                alerts = {
                    {type = 'warn', message = exp_1},
                    {type = 'warn', message = exp_2},
                },
            })
        end)
    end)

    -- Only one drop connection alert, no duplicates or extra
    -- warnings.
    t.assert_equals(#info.alerts, 2)

    -- A new connection is not accepted despite the alert.
    local uri = cluster['i-001'].net_box_uri
    g.conn = net_box.connect(uri)
    t.assert_equals(g.conn.state, 'error')
    t.assert_not(g.conn:ping())
end

-- Verify that an isolated instance refuses to replicate data from
-- other replicaset members.
--
-- The test case verifies that it occurs on a runtime
-- reconfiguration as well as on a startup.
g.test_replication_to = function(g)
    local function instance_uri(instance_name)
        return {
            login = 'replicator',
            password = 'secret',
            uri = ('unix/:./%s.iproto'):format(instance_name),
        }
    end

    local function assert_isolated(exp)
        g.it:roundtrip("require('config'):get('isolated')", exp)
    end

    local function assert_upstreams(exp)
        g.it:roundtrip('box.cfg.replication', exp)
    end

    local config = cbuilder:new()
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'i-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Use the console connection, because an instance in the
    -- isolated mode doesn't accept iproto requests.
    g.it = it.connect(cluster['i-003'])

    -- Mark i-003 as isolated, reload the configuration.
    local config_2 = cbuilder:new(config)
        :set_instance_option('i-003', 'isolated', true)
        :config()
    cluster:sync(config_2)
    g.it:roundtrip("require('config'):reload()")

    -- The isolated mode is applied, the instance has no
    -- upstreams.
    assert_isolated(true)
    assert_upstreams({})

    -- Restart i-003, reconnect the console.
    --
    -- Don't check the readiness condition, because it is
    -- performed over an iproto connection and it has no chance to
    -- succeed (iproto stops listening in the isolated mode).
    g.it:close()
    cluster['i-003']:stop()
    cluster['i-003']:start({wait_until_ready = false})
    connect_console(g, cluster['i-003'])

    -- Still in the isolated mode and no upstreams.
    assert_isolated(true)
    assert_upstreams({})

    -- Disable the isolated mode on i-003.
    local config_3 = cbuilder:new(config_2)
        :set_instance_option('i-003', 'isolated', nil)
        :config()
    cluster:sync(config_3)
    g.it:roundtrip("require('config'):reload()")

    -- Verify that the instance is configured now to fetch data
    -- from others.
    assert_isolated(false)
    assert_upstreams({
        instance_uri('i-001'),
        instance_uri('i-002'),
        instance_uri('i-003'),
    })
end

-- Verify that neither of replicaset members fetch data from an
-- isolated instance.
g.test_replication_from = function(g)
    local function instance_uri(instance_name)
        return {
            login = 'replicator',
            password = 'secret',
            uri = ('unix/:./%s.iproto'):format(instance_name),
        }
    end

    local function reload(server)
        server:exec(function()
            local config = require('config')

            config:reload()
        end)
    end

    local function assert_upstreams(server, exp)
        server:exec(function(exp)
            t.assert_equals(box.cfg.replication, exp)
        end, {exp})
    end

    local config = cbuilder:new()
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'i-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Verify a test case prerequisite: i-003 is in the upstreams
    -- list.
    assert_upstreams(cluster['i-001'], {
        instance_uri('i-001'),
        instance_uri('i-002'),
        instance_uri('i-003'),
    })
    assert_upstreams(cluster['i-002'], {
        instance_uri('i-001'),
        instance_uri('i-002'),
        instance_uri('i-003'),
    })

    -- Mark i-003 as isolated, reload the configuration on others.
    local config_2 = cbuilder:new(config)
        :set_instance_option('i-003', 'isolated', true)
        :config()
    cluster:sync(config_2)
    reload(cluster['i-001'])
    reload(cluster['i-002'])

    -- Verify that i-003 is not in the upstreams list.
    assert_upstreams(cluster['i-001'], {
        instance_uri('i-001'),
        instance_uri('i-002'),
    })
    assert_upstreams(cluster['i-002'], {
        instance_uri('i-001'),
        instance_uri('i-002'),
    })

    -- Disable the isolated mode on i-003, reload the
    -- configuration on others.
    local config_3 = cbuilder:new(config_2)
        :set_instance_option('i-003', 'isolated', nil)
        :config()
    cluster:sync(config_3)
    reload(cluster['i-001'])
    reload(cluster['i-002'])

    -- Verify that i-003 is now in the upstreams list again.
    assert_upstreams(cluster['i-001'], {
        instance_uri('i-001'),
        instance_uri('i-002'),
        instance_uri('i-003'),
    })
    assert_upstreams(cluster['i-002'], {
        instance_uri('i-001'),
        instance_uri('i-002'),
        instance_uri('i-003'),
    })
end

-- Verify that an alert is issued on an isolated instance.
g.test_alert = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Connect to the server's console.
    g.it = it.connect(cluster['i-001'])

    -- Verify a test case prerequisite: no alerts.
    local info = g.it:roundtrip("require('config'):info()")
    t.assert_equals(info.alerts, {})

    -- Go to the isolated mode.
    local config_2 = cbuilder:new(config)
        :set_instance_option('i-001', 'isolated', true)
        :config()
    cluster:sync(config_2)
    g.it:roundtrip("require('config'):reload()")

    -- Verify that the alert is issued.
    local exp = 'The isolated mode is set for the instance "i-001"'
    local info = g.it:roundtrip("require('config'):info()")
    t.assert_covers(info.alerts, {
        {type = 'warn', message = exp},
    })

    -- Only one alert, no duplicates or extra warnings.
    t.assert_equals(#info.alerts, 1)

    -- Disable the isolated mode on i-001.
    local config_3 = cbuilder:new(config_2)
        :set_instance_option('i-001', 'isolated', nil)
        :config()
    cluster:sync(config_3)
    g.it:roundtrip("require('config'):reload()")

    -- The alert disappears.
    local info = g.it:roundtrip("require('config'):info()")
    t.assert_equals(info.alerts, {})
end

-- Similar to the previous one, but enables the isolated mode on
-- startup.
--
-- The test case mostly to verify that the alert is not
-- duplicated.
g.test_alert_on_startup = function(g)
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Stop i-001. It leaves a local snapshot, so it can be
    -- started in the isolated mode.
    cluster['i-001']:stop()

    -- Enable the isolated mode, write the new config.
    local config_2 = cbuilder:new(config)
        :set_instance_option('i-001', 'isolated', true)
        :config()
    cluster:sync(config_2)

    -- Start the instance from the local snapshot in the
    -- isolated mode.
    --
    -- Don't check the readiness condition, because it is
    -- performed over an iproto connection and it has no
    -- chance to succeed (iproto stops listening in the
    -- isolated mode).
    cluster['i-001']:start({wait_until_ready = false})

    -- Use the console connection, because an instance in the
    -- isolated mode doesn't accept iproto requests.
    connect_console(g, cluster['i-001'])

    -- Verify that the alert is issued.
    local exp = 'The isolated mode is set for the instance "i-001"'
    local info = g.it:roundtrip("require('config'):info()")
    t.assert_covers(info.alerts, {
        {type = 'warn', message = exp},
    })

    -- Only one alert, no duplicates or extra warnings.
    t.assert_equals(#info.alerts, 1)

    -- Disable the isolated mode on i-001.
    local config_3 = cbuilder:new(config_2)
        :set_instance_option('i-001', 'isolated', nil)
        :config()
    cluster:sync(config_3)
    g.it:roundtrip("require('config'):reload()")

    -- The alert disappears.
    local info = g.it:roundtrip("require('config'):info()")
    t.assert_equals(info.alerts, {})
end
