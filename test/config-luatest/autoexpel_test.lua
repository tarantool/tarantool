local yaml = require('yaml')
local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')
local cbuilder = require('luatest.cbuilder')
local cluster = require('test.config-luatest.cluster')

local g = t.group()

g.before_all(cluster.init)
g.after_each(cluster.drop)
g.after_all(cluster.clean)

-- {{{ Helpers

local function assert_rs(server, exp)
    server:exec(function(exp)
        local res = box.space._cluster:pairs():map(function(t)
            return t[3]
        end):totable()
        table.sort(res)
        t.assert_equals(res, exp)
    end, {exp})
end

local function reload(server)
    server:exec(function()
        local config = require('config')

        config:reload()
    end)
end

local function promote(server)
    server:exec(function()
        local fiber = require('fiber')

        t.helpers.retrying({timeout = 60}, function()
            -- Run box.ctl.promote() in background, because it
            -- seems that there are situations, when this call
            -- continues infinitely.
            --
            -- TODO: This behavior is surprising and we possibly
            -- should consider it as a bug.
            fiber.new(box.ctl.promote)
            t.helpers.retrying({timeout = 1}, function()
                assert(box.info.ro == false)
            end)
        end)
    end)
end

local function errinj_wal_io(server, enabled)
    server:exec(function(enabled)
        box.error.injection.set('ERRINJ_WAL_IO', enabled)
    end, {enabled})
end

local function find_alert(server, prefix)
    return server:exec(function(prefix)
        for _, alert in ipairs(box.info.config.alerts) do
            if alert.message:startswith(prefix) then
                return alert
            end
        end
        return nil
    end, {prefix})
end

-- Shortcut to make test cases more readable.
local function wait(f, ...)
    return t.helpers.retrying({timeout = 60}, f, ...)
end

local function wait_alert(server, prefix)
    return wait(function()
        local res = find_alert(server, prefix)
        assert(res ~= nil)
        return res
    end)
end

-- }}} Helpers

-- Verify that the autoexpelling reacts on the configuration
-- reloading.
g.test_works_on_reload = function(g)
    local config = cbuilder:new()
        :set_replicaset_option('replication.autoexpel', {
            enabled = true,
            by = 'prefix',
            prefix = 'i-'
        })
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'i-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        -- i-004 matches the prefix, so will be expelled when
        -- removed from the configuration.
        :add_instance('i-004', {})
        -- x-005 doesn't match the prefix, so it will be kept,
        -- when removed from the configuration.
        :add_instance('x-005', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Test case prerequisite.
    assert_rs(cluster['i-001'], {'i-001', 'i-002', 'i-003', 'i-004', 'x-005'})

    local config_2 = cbuilder:new(config)
        :set_replicaset_option('instances.i-004', nil)
        :set_replicaset_option('instances.x-005', nil)
        :config()
    cluster:sync(config_2)
    reload(cluster['i-001'])

    -- Don't assume that the autoexpelling effects immediately
    -- after the configuration reload. It is possible that the
    -- instance is temporarily in RO due to replication
    -- reconfiguration and goes to RW soon. After that it performs
    -- the expelling.
    wait(assert_rs, cluster['i-001'], {'i-001', 'i-002', 'i-003', 'x-005'})
end

-- Verify that the autoexpelling reacts on going to RW.
g.test_works_on_rw = function(g)
    local config = cbuilder:new()
        :set_replicaset_option('replication.autoexpel', {
            enabled = true,
            by = 'prefix',
            prefix = 'i-'
        })
        :set_replicaset_option('replication.failover', 'election')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        -- i-004 matches the prefix, so will be expelled when
        -- removed from the configuration.
        :add_instance('i-004', {})
        -- x-005 doesn't match the prefix, so it will be kept,
        -- when removed from the configuration.
        :add_instance('x-005', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Promote i-002 to leaders to make i-001 RO.
    promote(cluster['i-002'])

    -- Test case precondition.
    assert_rs(cluster['i-001'], {'i-001', 'i-002', 'i-003', 'i-004', 'x-005'})

    -- Load config without i-004 and x-005 on i-001.
    local config_2 = cbuilder:new(config)
        :set_replicaset_option('instances.i-004', nil)
        :set_replicaset_option('instances.x-005', nil)
        :config()
    cluster:sync(config_2)
    reload(cluster['i-001'])

    -- i-001 is RO.
    assert_rs(cluster['i-001'], {'i-001', 'i-002', 'i-003', 'i-004', 'x-005'})

    -- When i-001 is RW, it can expel instances.
    promote(cluster['i-001'])
    wait(assert_rs, cluster['i-001'], {'i-001', 'i-002', 'i-003', 'x-005'})
end

-- Verify that if the autoexpelling fails, an alert is reported.
g.test_alert_on_write_error = function(g)
    t.tarantool.skip_if_not_debug(
        'errinj based test cases only work in the Debug build')

    local config = cbuilder:new()
        :set_replicaset_option('replication.autoexpel', {
            enabled = true,
            by = 'prefix',
            prefix = 'i-'
        })
        :set_replicaset_option('replication.failover', 'manual')
        :set_replicaset_option('leader', 'i-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :add_instance('i-004', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Test case prerequisite.
    assert_rs(cluster['i-001'], {'i-001', 'i-002', 'i-003', 'i-004'})

    -- Block WAL.
    errinj_wal_io(cluster['i-001'], true)

    local config_2 = cbuilder:new(config)
        :set_replicaset_option('instances.i-004', nil)
        :set_replicaset_option('instances.x-005', nil)
        :config()
    cluster:sync(config_2)
    reload(cluster['i-001'])

    -- Nothing changes: the WAL is blocked.
    assert_rs(cluster['i-001'], {'i-001', 'i-002', 'i-003', 'i-004'})

    local err_msg_prefix = 'autoexpel failed (reload the configuration ' ..
        'to retry): '
    local err_reason = 'Failed to write to disk'

    -- The alert regarding inability to perform the expelling is
    -- reported.
    local alert = wait_alert(cluster['i-001'], err_msg_prefix)
    t.assert(alert)
    t.assert_covers(alert, {
        type = 'warn',
        message = err_msg_prefix .. err_reason,
    })

    -- Unblock WAL.
    errinj_wal_io(cluster['i-001'], false)

    -- Autoexpel is not woken up yet. It is expected.
    assert_rs(cluster['i-001'], {'i-001', 'i-002', 'i-003', 'i-004'})
    local alert = find_alert(cluster['i-001'], err_msg_prefix)
    t.assert(alert)
    t.assert_covers(alert, {
        type = 'warn',
        message = err_msg_prefix .. err_reason,
    })

    -- We need the configuration reloading or box.status event to
    -- wake up the autoexpelling fiber. Do the reload and verify
    -- that the autoexpelling do the work (and drops the alert).
    reload(cluster['i-001'])
    wait(assert_rs, cluster['i-001'], {'i-001', 'i-002', 'i-003'})
    local alert = find_alert(cluster['i-001'], err_msg_prefix)
    t.assert_equals(alert, nil)
end

-- Verify that enabling the autoexpelling and configuring several
-- RW instances is forbidden.
g.test_error_on_multiple_rw_on_startup = function()
    local config = cbuilder:new()
        :set_replicaset_option('replication.autoexpel', {
            enabled = true,
            by = 'prefix',
            prefix = 'i-'
        })
        :add_instance('i-001', {database = {mode = 'rw'}})
        :add_instance('i-002', {database = {mode = 'rw'}})
        :add_instance('i-003', {})
        :config()

    -- Write config to a temporary directory.
    local dir = treegen.prepare_directory({}, {})
    local config_file = treegen.write_file(dir, 'config.yaml',
        yaml.encode(config))

    -- Run tarantool instance that is expected to exit
    -- immediately.
    local env = {}
    local args = {'--name', 'i-001', '--config', config_file}
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, env, args, opts)

    -- Verify the exit code and the error reported to stderr.
    local exp_err = 'replication.autoexpel.enabled = true doesn\'t support ' ..
        'the multi-master configuration'
    t.assert_covers(res, {
        exit_code = 1,
        stderr = ('LuajitError: %s\nfatal error, exiting the event loop')
            :format(exp_err),
    })
end

-- Similar to the previous one, but checks the error on the
-- configuration reloading.
g.test_error_on_multiple_rw_on_reload = function(g)
    local config = cbuilder:new()
        :set_replicaset_option('replication.autoexpel', {
            enabled = true,
            by = 'prefix',
            prefix = 'i-'
        })
        :add_instance('i-001', {database = {mode = 'rw'}})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :add_instance('i-004', {})
        :config()

    local cluster = cluster.new(g, config)
    cluster:start()

    -- Test case prerequisite.
    assert_rs(cluster['i-001'], {'i-001', 'i-002', 'i-003', 'i-004'})

    local exp_err = 'replication.autoexpel.enabled = true doesn\'t support ' ..
        'the multi-master configuration'

    -- Assign the second master, try to reload the configuration.
    --
    -- config:reload() should raise an error.
    local config_2 = cbuilder:new(config)
        :set_instance_option('i-002', 'database.mode', 'rw')
        :set_replicaset_option('instances.i-004', nil)
        :config()
    cluster:sync(config_2)
    t.assert_error_msg_equals(exp_err, reload, cluster['i-001'])

    -- i-004 is still registered.
    assert_rs(cluster['i-001'], {'i-001', 'i-002', 'i-003', 'i-004'})

    -- The alert regarding the misconfiguration is reported.
    local alert = find_alert(cluster['i-001'], exp_err)
    t.assert(alert)
    t.assert_covers(alert, {
        type = 'error',
        message = exp_err,
    })

    -- Leave only one master (but keep replication.failover = off).
    --
    -- It is considered OK.
    local config_3 = cbuilder:new(config_2)
        :set_instance_option('i-002', 'database.mode', nil)
        :config()
    cluster:sync(config_3)
    reload(cluster['i-001'])

    -- i-004 is expelled.
    wait(assert_rs, cluster['i-001'], {'i-001', 'i-002', 'i-003'})

    -- No alert.
    local alert = find_alert(cluster['i-001'], exp_err)
    t.assert_equals(alert, nil)
end
