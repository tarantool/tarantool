local t = require('luatest')
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

        t.assert_equals(health, {
            liveness = {
                verdict = true,
                checks = {},
            },
        })
        t.assert_equals(box.info().health, health)
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

g.test_liveness_errors_are_isolated = function(cg)
    cg.server:exec(function()
        t.assert_equals(box.ctl.liveness_probe({
            name = 'broken',
            check = function()
                error('bad check', 0)
            end,
        }), true)

        local info = box.info.health.liveness
        t.assert_equals(info.verdict, false)
        t.assert_equals(info.checks.broken.status, 'failed')
        t.assert_equals(info.checks.broken.alert_code,
                        'health.liveness.broken')
        t.assert_str_contains(info.checks.broken.reason, 'bad check')

        local health = require('internal.healthcheck')
        t.assert_equals(health.remove_liveness_probe('broken'), true)
    end)
end
