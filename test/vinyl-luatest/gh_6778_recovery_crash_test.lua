local common = require('test.vinyl-luatest.common')
local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.before_test('test_recovery_crash', function()
    g.server = server:new({
        alias = 'master',
        box_cfg = common.default_box_cfg(),
    })
    g.server:start()
end)

g.after_test('test_recovery_crash', function()
    g.server:drop()
end)

g.test_recovery_crash = function()
    g.server:exec(function()
        local s = box.schema.create_space('test', {engine = 'vinyl'})
        s:create_index('pk')
        s:replace{0}
        s:delete{0}
        s:create_index('sk', {parts = {{2, 'unsigned'}}})
    end)
    g.server:restart()
    g.server:exec(function()
        local t = require('luatest')
        local s = box.space.test
        t.assert_equals(s.index.pk:select(), {})
        t.assert_equals(s.index.sk:select(), {})
    end)
end
