local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local server = require('luatest.server')

local g = t.group('box_info_health')

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_default_health = function(cg)
    cg.server:exec(function()
        local health = box.info.health

        t.assert_equals(health.liveness, {
            verdict = true,
            checks = {},
        })
        t.assert_equals(health.readiness, {
            status = true,
            checks = {
                box = {
                    status = 'ready',
                },
            },
        })
        t.assert_equals(box.info().health, health)
    end)
end

g.test_health_with_config = function()
    local config = cbuilder:new()
        :use_group('g-001')
        :use_replicaset('r-001')
        :add_instance('i-001', {})
        :config()
    local cluster_obj = cluster:new(config)
    cluster_obj:start()

    cluster_obj['i-001']:exec(function()
        t.assert_equals(box.info.health.readiness.status, true)
        t.assert_equals(box.info.health.readiness.checks.config, {
            status = 'ready',
        })
        t.assert_equals(box.info.health.readiness.checks.box, {
            status = 'ready',
        })

        local health = require('internal.healthcheck')
        local config = require('config')
        local saved_status = config._status
        config._status = 'startup_in_progress'
        t.assert_equals(box.info.health.readiness.checks.config, {
            status = 'degraded',
            reason = 'startup_in_progress',
        })
        config._status = saved_status
        t.assert_equals(box.info.health.readiness.checks.config, {
            status = 'ready',
        })

        local call_count = 0
        t.assert_equals(health.add_health_check('counted', function()
            call_count = call_count + 1
            return true
        end, {alert = false}), true)
        t.assert_equals(box.info.health.readiness.checks.counted, {
            status = 'ready',
        })
        t.assert_equals(call_count, 1)
        t.assert_equals(box.info.config.alerts, {})
        t.assert_equals(call_count, 2)
        t.assert_equals(health.remove_health_check('counted'), true)

        t.assert_equals(health.add_health_check('app', function()
            return false, 'warming up'
        end, {alert_code = 'app.warming_up'}), true)
        t.assert_equals(box.info.health.readiness.checks.app, {
            status = 'not_ready',
            reason = 'warming up',
            alert_code = 'app.warming_up',
        })
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_items_include(box.info.config.alerts[1], {
            type = 'warn',
            message = 'Readiness health check "app" failed: warming up',
            alert_code = 'app.warming_up',
        })

        t.assert_equals(health.remove_health_check('app'), true)
        t.assert_equals(box.info.health.readiness.status, true)
        t.assert_equals(box.info.config.alerts, {})

        t.assert_equals(health.add_health_check('app', function()
            return false, 'starting'
        end, {alert_code = 'app.starting'}), true)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_items_include(box.info.config.alerts[1], {
            type = 'warn',
            message = 'Readiness health check "app" failed: starting',
            alert_code = 'app.starting',
        })
        t.assert_equals(health.remove_health_check('app'), true)
        t.assert_equals(box.info.config.alerts, {})

        t.assert_equals(box.ctl.liveness_probe({
            name = 'watchdog',
            check = function()
                return false, 'stopped'
            end,
        }), true)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_items_include(box.info.config.alerts[1], {
            type = 'warn',
            message = 'Liveness health check "watchdog" failed: stopped',
            alert_code = 'health.liveness.watchdog',
        })
        t.assert_equals(health.remove_liveness_probe('watchdog'), true)
        t.assert_equals(box.info.config.alerts, {})

        t.assert_equals(box.ctl.liveness_probe({
            name = 'unalerted',
            check = function()
                return false, 'silent'
            end,
            alert = false,
        }), true)
        t.assert_equals(box.info.health.liveness.checks.unalerted, {
            status = 'failed',
            reason = 'silent',
        })
        t.assert_equals(box.info.config.alerts, {})
        t.assert_equals(health.remove_liveness_probe('unalerted'), true)

        t.assert_equals(box.ctl.liveness_probe({
            name = 'custom-alert',
            check = function()
                return false, 'stalled'
            end,
            alert_code = 'app.liveness',
        }), true)
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_items_include(box.info.config.alerts[1], {
            type = 'warn',
            message = 'Liveness health check "custom-alert" failed: stalled',
            alert_code = 'app.liveness',
        })
        t.assert_equals(health.remove_liveness_probe('custom-alert'), true)
        t.assert_equals(box.info.config.alerts, {})

        t.assert_equals(box.ctl.liveness_probe({
            name = 'event-loop',
            check = function()
                return false, 'stalled'
            end,
        }), true)
        t.assert_equals(box.info.health.liveness.checks['event-loop'], {
            status = 'failed',
            reason = 'stalled',
            alert_code = 'health.liveness.event-loop',
        })
        t.assert_equals(#box.info.config.alerts, 1)
        t.assert_items_include(box.info.config.alerts[1], {
            type = 'warn',
            message = 'Liveness health check "event-loop" failed: stalled',
            alert_code = 'health.liveness.event-loop',
        })

        t.assert_equals(health.remove_liveness_probe('event-loop'), true)
        t.assert_equals(box.info.health.liveness.verdict, true)
        t.assert_equals(box.info.config.alerts, {})
    end)

    cluster_obj:drop()
end

g.test_readiness_registry = function(cg)
    cg.server:exec(function()
        local health = require('internal.healthcheck')

        t.assert_equals(health.add_health_check('app', function()
            return false, 'warming up'
        end), true)

        local info = box.info.health.readiness
        t.assert_equals(info.status, false)
        t.assert_equals(info.checks.app, {
            status = 'not_ready',
            reason = 'warming up',
            alert_code = 'health.readiness.app',
        })

        local ok, err = health.add_health_check('app', function()
            return true
        end)
        t.assert_equals(ok, false)
        t.assert_str_contains(err, 'already exists')

        t.assert_equals(health.remove_health_check('app'), true)
        t.assert_equals(box.info.health.readiness.status, true)
    end)
end

g.test_liveness_probe = function(cg)
    cg.server:exec(function()
        local ok, err = box.ctl.liveness_probe({
            name = 'event-loop',
            check = function()
                return nil, 'stalled'
            end,
        })
        t.assert_equals(ok, true)
        t.assert_equals(err, nil)

        local liveness = box.info.health.liveness
        t.assert_equals(liveness.verdict, false)
        t.assert_equals(liveness.checks['event-loop'], {
            status = 'failed',
            reason = 'stalled',
            alert_code = 'health.liveness.event-loop',
        })

        local health = require('internal.healthcheck')
        t.assert_equals(health.remove_liveness_probe('event-loop'), true)
        t.assert_equals(box.info.health.liveness.verdict, true)
    end)
end

g.test_yielding_check_fails = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local health = require('internal.healthcheck')

        t.assert_equals(box.ctl.liveness_probe({
            name = 'yielding',
            check = function()
                fiber.sleep(0)
                return true
            end,
        }), true)

        local liveness = box.info.health.liveness
        t.assert_equals(liveness.verdict, false)
        t.assert_equals(liveness.checks.yielding, {
            status = 'failed',
            reason = 'health check must not yield',
            alert_code = 'health.liveness.yielding',
        })
        t.assert_equals(health.remove_liveness_probe('yielding'), true)

        t.assert_equals(health.add_health_check('yielding', function()
            fiber.sleep(0)
            return true
        end), true)

        local readiness = box.info.health.readiness
        t.assert_equals(readiness.status, false)
        t.assert_equals(readiness.checks.yielding, {
            status = 'not_ready',
            reason = 'health check must not yield',
            alert_code = 'health.readiness.yielding',
        })
        t.assert_equals(health.remove_health_check('yielding'), true)
    end)
end

g.test_check_errors_are_isolated = function(cg)
    cg.server:exec(function()
        local health = require('internal.healthcheck')
        t.assert_equals(health.add_health_check('broken', function()
            error('bad check', 0)
        end), true)

        local info = box.info.health.readiness
        t.assert_equals(info.status, false)
        t.assert_equals(info.checks.broken.status, 'not_ready')
        t.assert_equals(info.checks.broken.alert_code,
                        'health.readiness.broken')
        t.assert_str_contains(info.checks.broken.reason, 'bad check')

        t.assert_equals(health.remove_health_check('broken'), true)
    end)
end

g.test_recursive_health_evaluation_is_rejected = function(cg)
    cg.server:exec(function()
        local health = require('internal.healthcheck')

        t.assert_equals(box.ctl.liveness_probe({
            name = 'recursive',
            check = function()
                return health.info().liveness.verdict
            end,
        }), true)

        local liveness = box.info.health.liveness
        t.assert_equals(liveness.verdict, false)
        t.assert_equals(liveness.checks.recursive.status, 'failed')
        t.assert_str_contains(liveness.checks.recursive.reason,
                              'recursive health check evaluation')
        t.assert_equals(health.remove_liveness_probe('recursive'), true)

        t.assert_equals(health.add_health_check('recursive', function()
            return health.info().readiness.status
        end), true)

        local readiness = box.info.health.readiness
        t.assert_equals(readiness.status, false)
        t.assert_equals(readiness.checks.recursive.status, 'not_ready')
        t.assert_str_contains(readiness.checks.recursive.reason,
                              'recursive health check evaluation')
        t.assert_equals(health.remove_health_check('recursive'), true)

        local call_count = 0
        t.assert_equals(health.add_health_check('config-alerts', function()
            call_count = call_count + 1
            t.assert_equals(box.info.config.alerts, {})
            return true
        end, {alert = false}), true)
        t.assert_equals(box.info.health.readiness.checks['config-alerts'], {
            status = 'ready',
        })
        t.assert_equals(call_count, 1)
        t.assert_equals(health.remove_health_check('config-alerts'), true)
    end)
end

g.test_readiness_degraded_after_ready = function(cg)
    cg.server:exec(function()
        local health = require('internal.healthcheck')
        local mode = 'ready'

        t.assert_equals(health.add_health_check('flaky', function()
            if mode == 'ready' then
                return true
            end
            error('lost dependency', 0)
        end), true)

        local info = box.info.health.readiness
        t.assert_equals(info.status, true)
        t.assert_equals(info.checks.flaky, {
            status = 'ready',
        })

        mode = 'failed'
        info = box.info.health.readiness
        t.assert_equals(info.status, false)
        t.assert_equals(info.checks.flaky.status, 'degraded')
        t.assert_equals(info.checks.flaky.alert_code,
                        'health.readiness.flaky')
        t.assert_str_contains(info.checks.flaky.reason, 'lost dependency')

        mode = 'ready'
        info = box.info.health.readiness
        t.assert_equals(info.status, true)
        t.assert_equals(info.checks.flaky, {
            status = 'ready',
        })

        t.assert_equals(health.remove_health_check('flaky'), true)
    end)
end

g.test_invalid_check_result = function(cg)
    cg.server:exec(function()
        local health = require('internal.healthcheck')
        t.assert_equals(health.add_health_check('invalid', function()
            return nil
        end), true)

        local info = box.info.health.readiness
        t.assert_equals(info.status, false)
        t.assert_equals(info.checks.invalid, {
            status = 'not_ready',
            reason = 'health check must return true or false, <string>',
            alert_code = 'health.readiness.invalid',
        })

        t.assert_equals(health.remove_health_check('invalid'), true)

        t.assert_equals(health.add_health_check('invalid', function()
            return {
                app = {
                    status = 'not_ready',
                    reason = 'failed',
                },
            }
        end), true)

        info = box.info.health.readiness
        t.assert_equals(info.status, false)
        t.assert_equals(info.checks.invalid, {
            status = 'not_ready',
            reason = 'health check must return true or false, <string>',
            alert_code = 'health.readiness.invalid',
        })

        t.assert_equals(health.remove_health_check('invalid'), true)
    end)
end
