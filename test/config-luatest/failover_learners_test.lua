local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group()

-- {{{ Helpers

local function check_instance_mode(instance, mode)
    t.assert_equals(instance:eval('return box.info.ro'), mode == 'ro')
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

-- }}} Helpers

-- Verify that using `replication.failover = "supervised"` with
-- `replication.bootstrap_strategy = "auto"` not chooses failover
-- learners as a bootstrap leader.
g.test_not_chooses_learners_as_bootstrap_leader = function()
    local config = cbuilder:new()
        :set_global_option('failover.replicasets.r-001.learners',
                           {'i-001', 'i-002'})
        :set_global_option('replication.failover', 'supervised')
        :use_replicaset('r-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :config()

    local cluster = cluster:new(config)
    cluster:start()

    check_instance_mode(cluster['i-001'], 'ro')
    check_instance_mode(cluster['i-002'], 'ro')
    check_instance_mode(cluster['i-003'], 'rw')
end

-- Check a singleton replicaset bootstrap fails if the instance
-- is marked as a learner.
g.test_all_learners = function()
    local config = cbuilder:new()
        :set_global_option('failover.replicasets.r-001.learners',
                           {'i-001'})
        :set_global_option('replication.failover', 'supervised')
        :use_replicaset('r-001')
        :add_instance('i-001', {})
        :config()

    local cluster = cluster:new(config)
    cluster:start({wait_until_ready = false})

    -- The instance (with the smallest UUID in the
    -- lexicographical order) chooses itself as a join bootstrap
    -- leader and fails since it's unable to bootstrap itself in
    -- RO mode.
    --
    -- If there are other instances within the replicaset the
    -- behavior is unpredictable. They might connect to the
    -- leader before it fails. If so they will wait for it.
    -- Otherwise they will fail.
    t.helpers.retrying({timeout = 10}, function()
        t.assert_not(cluster['i-001'].process:is_alive())
    end)

    local config = cbuilder:new(config)
        :set_global_option('failover.replicasets.r-001.learners', {})
        :config()
    cluster:sync(config)

    -- Now, the cluster should be able to bee successfully
    -- bootstrapped.
    cluster:start()

    check_instance_mode(cluster['i-001'], 'rw')
end

-- Check an alert is issued if all instances are marked as
-- learners.
g.test_alert_on_no_leader = function()
    local config1 = cbuilder:new()
        :use_replicaset('r-001')
        :add_instance('i-001', {})
        :config()

    local cluster = cluster:new(config1)
    cluster:start()

    local config2 = cbuilder:new(config1)
        :set_global_option('failover.replicasets.r-001.learners',
                           {'i-001'})
        :set_global_option('replication.failover', 'supervised')
        :config()
    cluster:reload(config2)

    local msg = 'box_cfg.apply: cannot determine a bootstrap leader based ' ..
                'on the configuration'
    find_alert(cluster['i-001'], msg)

    cluster:reload(config1)
    cluster['i-001']:exec(function()
        t.assert_equals(box.info.config.alerts, {})
    end)
end
