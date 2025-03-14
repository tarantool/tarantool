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

-- Check if a learner has been chosen as a bootstrap leader
-- (due to the fact there are no non-learner candidates)
-- the configuration alert is issued.
g.test_all_learners = function()
    local config = cbuilder:new()
        :set_global_option('failover.replicasets.r-001.learners',
                           {'i-001', 'i-002', 'i-003'})
        :set_global_option('replication.failover', 'supervised')
        :use_replicaset('r-001')
        :add_instance('i-001', {})
        :add_instance('i-002', {})
        :add_instance('i-003', {})
        :config()

    local cluster = cluster:new(config)
    cluster:start()

    check_instance_mode(cluster['i-001'], 'rw')

    local alert_prefix = 'box_cfg.apply: the only available instance for ' ..
                         'replicaset bootstrap is marked as a learner'
    find_alert(cluster['i-001'], alert_prefix)

    config = cbuilder:new(config)
        :set_global_option('failover.replicasets.r-001.learners', {'i-001'})
        :config()
    cluster:reload(config)

    cluster['i-001']:exec(function()
        t.assert_equals(box.info.config.alerts, {})
    end)
end
