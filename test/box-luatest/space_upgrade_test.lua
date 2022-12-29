local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.schema.create_space('test')
    end)
end)

g.after_all(function()
    g.server:stop()
end)

g.test_low_level_api = function()
    t.tarantool.skip_if_enterprise()
    g.server:exec(function()
        local t = require('luatest')
        t.assert_error_msg_equals(
            "Community edition does not support space upgrade",
            box.space._space.update, box.space._space,
            box.space.test.id, {{'=', 'flags.upgrade', {}}})
    end)
end

g.test_high_level_api = function()
    t.tarantool.skip_if_enterprise()
    g.server:exec(function()
        local t = require('luatest')
        t.assert_error_msg_equals(
            "Community edition does not support space upgrade",
            box.space.test.upgrade, box.space.test, {})
    end)
end
