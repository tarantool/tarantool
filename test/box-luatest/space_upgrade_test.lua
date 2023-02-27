local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.schema.create_space('memtx')
        box.schema.create_space('vinyl', {engine = 'vinyl'})
    end)
end)

g.after_all(function()
    g.server:stop()
end)

g.test_low_level_api = function()
    t.tarantool.skip_if_enterprise()
    g.server:exec(function()
        t.assert_error_msg_equals(
            "Community edition does not support space upgrade",
            box.space._space.update, box.space._space,
            box.space.memtx.id, {{'=', 'flags.upgrade', {}}})
        t.assert_error_msg_equals(
            "vinyl does not support space upgrade",
            box.space._space.update, box.space._space,
            box.space.vinyl.id, {{'=', 'flags.upgrade', {}}})
        t.assert_error_msg_equals(
            "sysview does not support space upgrade",
            box.space._space.update, box.space._space,
            box.space._vspace.id, {{'=', 'flags.upgrade', {}}})
    end)
end

g.test_high_level_api = function()
    t.tarantool.skip_if_enterprise()
    g.server:exec(function()
        local errmsg = "Community edition does not support space upgrade"
        t.assert_error_msg_equals(errmsg, box.space.memtx.upgrade,
                                  box.space.memtx, {})
        t.assert_error_msg_equals(errmsg, box.space.vinyl.upgrade,
                                  box.space.vinyl, {})
        t.assert_error_msg_equals(errmsg, box.space._vspace.upgrade,
                                  box.space._vspace, {})
    end)
end
