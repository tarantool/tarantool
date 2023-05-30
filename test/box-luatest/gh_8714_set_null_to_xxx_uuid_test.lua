local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_test('test_set_null_to_xxx_uuid', function()
    g.server = server:new({
        alias = 'master',
        box_cfg = {
            instance_uuid = box.NULL,
            replicaset_uuid = box.NULL,
        },
    })
    g.server:start()
end)

g.after_test('test_set_null_to_xxx_uuid', function()
    g.server:stop()
end)

g.test_set_null_to_xxx_uuid = function()
    g.server:exec(function()
        local ok = pcall(box.cfg, {instance_uuid = box.NULL})
        t.assert(ok)
        ok = pcall(box.cfg, {replicaset_uuid = box.NULL})
        t.assert(ok)
    end)
end

g.before_test('test_set_uuid_to_xxx_uuid', function()
    g.server = server:new({
        alias = 'master',
        box_cfg = {
            instance_uuid = '11111111-1111-1111-1111-111111111111',
            replicaset_uuid = '11111111-1111-1111-1111-111111111111',
        },
    })
    g.server:start()
end)

g.after_test('test_set_uuid_to_xxx_uuid', function()
    g.server:stop()
end)

g.test_set_uuid_to_xxx_uuid = function()
    g.server:exec(function()
        local old_id = '11111111-1111-1111-1111-111111111111'
        local new_id = '11111111-1111-1111-1111-111111111112'
        local err_ins = "Can't set option 'instance_uuid' dynamically"
        local err_rpl = "Can't set option 'replicaset_uuid' dynamically"

        -- The same uuid can be set if *_uuid is set.
        local ok = pcall(box.cfg, {instance_uuid = old_id})
        t.assert(ok)
        ok = pcall(box.cfg, {replicaset_uuid = old_id})
        t.assert(ok)

        -- Another uuid cannot be set if *_uuid is set.
        t.assert_error_msg_equals(err_ins, box.cfg, {instance_uuid = new_id})
        t.assert_error_msg_equals(err_rpl, box.cfg, {replicaset_uuid = new_id})

        -- NULL can be set even if *_uuid is set.
        ok = pcall(box.cfg, {instance_uuid = box.NULL})
        t.assert(ok)
        ok = pcall(box.cfg, {replicaset_uuid = box.NULL})
        t.assert(ok)

        -- Another uuid cannot be set if *_uuid is NULL.
        t.assert_error_msg_equals(err_ins, box.cfg, {instance_uuid = new_id})
        t.assert_error_msg_equals(err_rpl, box.cfg, {replicaset_uuid = new_id})

        -- Old uuid can be set if *_uuid is NULL.
        ok = pcall(box.cfg, {instance_uuid = old_id})
        t.assert(ok)
        ok = pcall(box.cfg, {replicaset_uuid = old_id})
        t.assert(ok)
    end)
end
