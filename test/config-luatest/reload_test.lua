local fio = require('fio')
local fiber = require('fiber')
local t = require('luatest')
local treegen = require('luatest.treegen')
local server = require('luatest.server')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group()

-- Shortcut to make test cases more readable.
local function wait(f, ...)
    return t.helpers.retrying({timeout = 60}, f, ...)
end

-- {{{ Testing helpers

-- Add :pause_next_reload() and :continue_reload() methods to the
-- server objects.
--
-- This function adds a role into the configuration to implement
-- the functions. So, it returns the new configuration.
local function define_pause_reload_methods(cluster, config)
    -- Write the role.
    treegen.write_file(cluster._dir, 'pause_reload.lua', string.dump(function()
        local fiber = require('fiber')

        local pause_reload = false

        rawset(_G, 'pause_next_reload', function()
            pause_reload = true
        end)

        rawset(_G, 'continue_reload', function()
            pause_reload = false
        end)

        return {
            apply = function(_cfg)
                while pause_reload do
                    fiber.sleep(0.01)
                end
            end,
            validate = function(_cfg) end,
            stop = function() end,
        }
    end))

    -- Add the role into the configuration.
    local new_config = cbuilder:new(config)
        :set_global_option('roles', {'pause_reload'})
        :config()
    cluster:sync(new_config)

    local function pause_next_reload(self)
        self:call('pause_next_reload')
    end

    local function continue_reload(self)
        self:call('continue_reload')
    end

    -- Define the methods.
    cluster:each(function(server)
        server.pause_next_reload = pause_next_reload
        server.continue_reload = continue_reload
    end)

    return new_config
end

-- Add :sigusr2() method to the server objects.
local function define_sigusr2_method(cluster)
    local function sigusr2(self)
        self.process:kill('USR2')
    end

    cluster:each(function(server)
        server.sigusr2 = sigusr2
    end)
end

-- Add :process_title() method to the server objects.
local function define_process_title_method(cluster)
    local function process_title(self)
        return self:exec(function()
            local config = require('config')

            return config:get('process.title')
        end)
    end

    cluster:each(function(server)
        server.process_title = process_title
    end)
end

-- }}} Testing helpers

g.after_each(function()
    if g.server ~= nil then
        g.server:stop()
    end
end)

g.test_no_cluster_config_failure = function(g)
    local dir = treegen.prepare_directory({}, {})
    local config = [[
        credentials:
          users:
            guest:
              roles:
              - super
        iproto:
          listen:
            - uri: unix/:./{{ instance_name }}.iproto
        groups:
          group-001:
            replicasets:
              replicaset-001:
                instances:
                  instance-001: {}
    ]]
    local config_file = treegen.write_file(dir, 'config.yaml', config)
    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = dir,
    }
    g.server = server:new(opts)
    g.server:start()

    --
    -- Make sure that the error starts with "Reload" if the configuration did
    -- not contain a cluster configuration at the time of the reload.
    --
    config = [[{}]]
    treegen.write_file(dir, 'config.yaml', config)
    g.server:exec(function()
        local config = require('config')
        local ok, err = pcall(config.reload, config)
        t.assert(not ok)
        t.assert(string.startswith(err, 'Reload failure.\n\nNo cluster config'))
    end)

    --
    -- Make sure that the error starts with "Reload" if the instance was not
    -- found in the cluster configuration at the time of the reload.
    --
    config = [[
        credentials:
          users:
            guest:
              roles:
              - super
        iproto:
          listen:
            - uri: unix/:./{{ instance_name }}.iproto
    ]]
    treegen.write_file(dir, 'config.yaml', config)
    g.server:exec(function()
        local config = require('config')
        local ok, err = pcall(config.reload, config)
        t.assert(not ok)
        t.assert(string.startswith(err, 'Reload failure.\n\nUnable to find'))
    end)
end

-- Verify that tarantool reloads the YAML configuration if SIGUSR2
-- is arrived.
g.test_reload_by_signal_basic = function()
    -- Start one instance.
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :config()
    local cluster = cluster:new(config)

    define_sigusr2_method(cluster)
    define_process_title_method(cluster)

    cluster:start()

    -- Write a new config.
    local config_2 = cbuilder:new(config)
        :set_global_option('process.title', 'configuration 2')
        :config()
    cluster:sync(config_2)

    -- Send SIGUSR2.
    cluster['i-001']:sigusr2()

    -- Verify that the new config is loaded.
    wait(function()
        t.assert_equals(cluster['i-001']:process_title(), 'configuration 2')
    end)
end

-- Verify that tarantool is not terminated by SIGUSR2 if the
-- application is started from a Lua script (tarantool 2.x way).
g.test_no_termination_by_sigusr2 = function(g)
    -- Start one instance.
    g.server = server:new({alias = 'i-001'})
    g.server:start()

    -- Send SIGUSR2 and wait a bit to let the signal arrive.
    g.server.process:kill('USR2')
    fiber.sleep(0.1)

    -- Verify that tarantool is not terminated.
    t.assert(g.server.process:is_alive())
end

-- Verify that if a signal arrives during the reloading, the next
-- reload will be started after finishing the current one.
g.test_signal_during_reload_by_signal = function()
    -- Define one instance.
    local config = cbuilder:new()
        :add_instance('i-001', {})
        :config()
    local cluster = cluster:new(config)

    -- Define needed methods.
    config = define_pause_reload_methods(cluster, config)
    define_sigusr2_method(cluster)
    define_process_title_method(cluster)

    -- Start the instance.
    cluster:start()

    -- Write a new config.
    local config_2 = cbuilder:new(config)
        :set_global_option('process.title', 'configuration 2')
        :config()
    cluster:sync(config_2)

    -- Setup a pause for the next reload and send SIGUSR2.
    cluster['i-001']:pause_next_reload()
    cluster['i-001']:sigusr2()

    -- Wait until the reload is started.
    --
    -- We need to wait here to ensure that the first signal is
    -- received and it is not coalesced with the next one.
    --
    -- See man 7 signal, 'Queueing and delivery semantics for
    -- standard signals' for details.
    --
    -- Also verify that we're currently applying the second
    -- configuration.
    wait(function()
        t.assert_equals(cluster['i-001']:process_title(), 'configuration 2')
    end)

    -- Update the config file once again and send one more signal.
    --
    -- NB: Loading of the second configuration is not finished
    -- yet, but we already sent a signal to load the third one.
    -- It should be queued.
    local config_3 = cbuilder:new(config_2)
        :set_global_option('process.title', 'configuration 3')
        :config()
    cluster:sync(config_3)
    cluster['i-001']:sigusr2()

    -- Continue reloading of the second configuration.
    --
    -- It finishes the second configuration loading and then
    -- loads the third configuration.
    cluster['i-001']:continue_reload()

    wait(function()
        t.assert_equals(cluster['i-001']:process_title(), 'configuration 3')
    end)
end

-- Send SIGUSR2 while the config:reload() call is in progress.
--
-- Currently, the reloading by a signal is discarded. This may be
-- changed in a future toward consistency: so all the reload
-- requests will be queued, not only ones triggered by a signal.
--
-- For a while, just hold the current behavior by a test case
-- and verify that an adequate log message is reported.
g.test_signal_during_reload_from_lua = function()
    -- Define one instance.
    local config = cbuilder:new()
        -- log.to is to workaround
        -- https://github.com/tarantool/luatest/issues/389
        :set_global_option('log.to', 'file')
        :add_instance('i-001', {})
        :config()
    local cluster = cluster:new(config)

    -- Define needed methods.
    config = define_pause_reload_methods(cluster, config)
    define_sigusr2_method(cluster)
    define_process_title_method(cluster)

    -- Start the instance.
    cluster:start()

    -- Write a new process.title value to the config file.
    local config_2 = cbuilder:new(config)
        :set_global_option('process.title', 'configuration 2')
        :config()
    cluster:sync(config_2)

    -- Pause the next reload and call config:reload().
    cluster['i-001']:pause_next_reload()
    cluster['i-001']:exec(function()
        local fiber = require('fiber')
        local config = require('config')

        fiber.new(function()
            fiber.name('do_config_reload')

            config:reload()
        end)
    end)

    -- Ensure that the reload is in progress.
    wait(function()
        t.assert_equals(cluster['i-001']:process_title(), 'configuration 2')
    end)

    -- Write a third configuration.
    local config_3 = cbuilder:new(config_2)
        :set_global_option('process.title', 'configuration 3')
        :config()
    cluster:sync(config_3)

    -- Attempt to trigger the reload using SIGUSR2. It is expected
    -- to fail. Verify that some adequate warning is logged.
    cluster['i-001']:sigusr2()
    wait(function()
        -- Workaround https://github.com/tarantool/luatest/issues/389
        local log_file = fio.pathjoin(cluster._dir,
            'var/log/i-001/tarantool.log')

        local exp = 'Configuration reloading by SIGUSR2 is failed: ' ..
            'config:reload%(%): instance configuration is already in progress'
        t.assert(cluster['i-001']:grep_log(exp, nil, {filename = log_file}))
    end)

    -- Unblock the reloading, wait a bit and ensure that the third
    -- configuration is NOT loaded.
    cluster['i-001']:continue_reload()
    fiber.sleep(0.1)
    t.assert_equals(cluster['i-001']:process_title(), 'configuration 2')
end
