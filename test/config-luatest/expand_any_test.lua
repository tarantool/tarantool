local t = require('luatest')
local helpers = require('test.config-luatest.helpers')

local g = helpers.group('expand_any')

g.test_context_vars_under_any = function(g)
    helpers.success_case(g, {
        options = {
            ['config.context.title'] = {from = 'env', env = 'TITLE'},
            ['process'] = {title = '{{ context.title }}'},
            ['app.cfg'] = {title = '{{ context.title }}'},
            ['app.cfg.number'] = 42,
            ['roles_cfg.foo'] = {title = '{{ context.title }}', flag = true},
        },
        env = {TITLE = 'xxx'},
        verify = function()
            local t = require('luatest')
            local config = require('config')
            t.assert_equals(config:get('process.title'), 'xxx')
            t.assert_equals(config:get('app.cfg.title'), 'xxx')
            t.assert_equals(config:get('roles_cfg.foo.title'), 'xxx')
            t.assert_equals(config:get('app.cfg.number'), 42)
            t.assert_equals(config:get('roles_cfg.foo.flag'), true)
        end,
    })
end

g.test_builtin_vars_under_any = function(g)
    helpers.success_case(g, {
        options = {
            ['process'] = {title = '{{ instance_name }}'},
            ['app.cfg'] = {title = '{{ instance_name }}'},
            ['roles_cfg.foo'] = {title = '{{ instance_name }}'},
        },
        verify = function()
            local config = require('config')
            t.assert_equals(config:get('process.title'), 'instance-001')
            t.assert_equals(config:get('app.cfg.title'), 'instance-001')
            t.assert_equals(config:get('roles_cfg.foo.title'), 'instance-001')
        end,
    })
end

g.test_any_transforms_keys = function(g)
    helpers.success_case(g, {
        options = {
            ['app.cfg.rpc_priority'] = {
                ['instance-002'] = 2,
                ['instance-003'] = 3,
                ['{{ instance_name }}'] = 99,
            },
        },
        verify = function()
            local config = require('config')
            local priorities = config:get('app.cfg.rpc_priority')
            t.assert_equals(priorities['instance-001'], 99)
            t.assert_equals(priorities['instance-002'], 2)
            t.assert_equals(priorities['instance-003'], 3)
            t.assert_equals(priorities['{{ instance_name }}'], nil)
        end,
    })
end
