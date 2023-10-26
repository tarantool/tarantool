local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun')

local g = t.group()

g.test_box_schema_before_box_cfg = function()
    treegen.init(g)
    local dir = treegen.prepare_directory(g, {}, {})
    treegen.write_script(dir, 'main.lua', [[
        local t = require('luatest')
        local schema = box.schema

        -- Allowed to call the following functions before box.cfg
        t.assert_equals(pcall(schema.downgrade_versions), true)

        -- Test, that it's impossible to invoke all other box.schema.*.
        local msg = 'Please call box.cfg{} first'

        -- t.assert_error_msg_contains(msg, schema.create_space, 'test')
        t.assert_error_msg_contains(msg, schema.upgrade)
        t.assert_error_msg_contains(msg, schema.downgrade, '3.0.0')
        t.assert_error_msg_contains(msg, schema.downgrade_issues, '3.0.0')

        t.assert_error_msg_contains(msg, schema.func.bless, {})
        t.assert_error_msg_contains(msg, schema.func.call, 'f')
        t.assert_error_msg_contains(msg, schema.func.create, 'f')
        t.assert_error_msg_contains(msg, schema.func.drop, 'f')
        t.assert_error_msg_contains(msg, schema.func.exists, 'f')
        t.assert_error_msg_contains(msg, schema.func.reload, 'f')

        t.assert_error_msg_contains(msg, schema.index.alter)
        t.assert_error_msg_contains(msg, schema.index.create)
        t.assert_error_msg_contains(msg, schema.index.drop)
        t.assert_error_msg_contains(msg, schema.index.rename, 1, 1, 'n')

        t.assert_error_msg_contains(msg, schema.role.create)
        t.assert_error_msg_contains(msg, schema.role.drop)
        t.assert_error_msg_contains(msg, schema.role.exists)
        t.assert_error_msg_contains(msg, schema.role.grant)
        t.assert_error_msg_contains(msg, schema.role.info)
        t.assert_error_msg_contains(msg, schema.role.revoke)

        t.assert_error_msg_contains(msg, schema.sequence.bless, {})
        t.assert_error_msg_contains(msg, schema.sequence.alter)
        t.assert_error_msg_contains(msg, schema.sequence.create, 'n')
        t.assert_error_msg_contains(msg, schema.sequence.drop, 'n')

        t.assert_error_msg_contains(msg, schema.space.alter, 's')
        t.assert_error_msg_contains(msg, schema.space.bless, {})
        t.assert_error_msg_contains(msg, schema.space.create, 's')
        t.assert_error_msg_contains(msg, schema.space.drop, 's')
        t.assert_error_msg_contains(msg, schema.space.format, 's')
        t.assert_error_msg_contains(msg, schema.space.rename, 1, 's')

        t.assert_error_msg_contains(msg, schema.user.create)
        t.assert_error_msg_contains(msg, schema.user.disable)
        t.assert_error_msg_contains(msg, schema.user.drop)
        t.assert_error_msg_contains(msg, schema.user.enable)
        t.assert_error_msg_contains(msg, schema.user.exists)
        t.assert_error_msg_contains(msg, schema.user.grant)
        t.assert_error_msg_contains(msg, schema.user.info)
        t.assert_error_msg_contains(msg, schema.user.passwd, 's', 's')
        t.assert_error_msg_contains(msg, schema.user.password)
        t.assert_error_msg_contains(msg, schema.user.revoke)
    ]])

    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, {'main.lua'}, opts)
    t.assert_equals(res.stderr, "")
    t.assert_equals(res.exit_code, 0)
end
