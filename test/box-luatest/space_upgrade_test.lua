local server = require('luatest.server')
local t = require('luatest')

local g = t.group('ce')

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

local g_ee = t.group('ee')

g_ee.before_all(function(cg)
    t.tarantool.skip_if_not_enterprise(
        'Space upgrade is supported only by Tarantool Enterprise Edition')
    cg.server = server:new({
        alias = 'master',
        box_cfg = {memtx_use_mvcc_engine = true},
    })
    cg.server:start()
end)

g_ee.after_all(function(cg)
    cg.server:drop()
end)

g_ee.test_replace_rollback_on_result_upgrade_error = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('memtx')
        s:create_index('pk')
        s:insert({1, 1})

        rawset(_G, 'fail_upgrade', true)
        box.schema.func.create('upgrade', {
            language = 'lua',
            is_deterministic = true,
            body = [[function(t)
                if rawget(_G, 'fail_upgrade') then
                    error('upgrade failed')
                end
                return t
            end]],
        })

        box.space._space:update(s.id, {{
            '=',
            'flags.upgrade',
            {
                owner = box.info.uuid,
                func = box.func.upgrade.id,
            },
        }})

        t.assert_error_msg_contains('upgrade failed', s.replace, s, {1, 2})
        rawset(_G, 'fail_upgrade', false)
        t.assert_equals(s:get({1}), {1, 1})
    end)
end
