local t = require('luatest')
local g = t.group('gh-7149')
local server = require('luatest.server')

g.after_each(function()
    g.server:drop()
end)

g.before_test('test_schema_access', function()
    g.server = server:new{alias = 'master'}
    g.server:start()
end)

-- Check that get_version() from upgrade.lua can be executed by any user without
-- getting an error: Read access to space '_schema' is denied for user 'user'.
g.test_schema_access = function()
    g.server:exec(function()
        box.cfg()
        box.schema.user.create('user')
        box.session.su('user')
        box.schema.upgrade()
    end)
end

g.before_test('test_ddl_ops', function()
    -- Recover from Tarantool 1.10 snapshot
    local data_dir = 'test/box-luatest/upgrade/1.10'
    -- Disable automatic schema upgrade
    local box_cfg = {read_only = true}
    g.server = server:new{alias = 'master',
                          datadir = data_dir,
                          box_cfg = box_cfg}
    g.server:start()
end)

g.test_ddl_ops = function()
    g.server:exec(function()
        local t = require('luatest')
        local error_msg = "DDL operations are not allowed: " ..
                          "Your schema version is 1.10.0 while Tarantool "

        -- Note that automatic schema upgrade will not be performed
        box.cfg{read_only = false}

        t.assert_error_msg_contains(error_msg,
            function() box.schema.space.create('test') end)
        t.assert_error_msg_contains(error_msg,
            function() box.schema.space.alter(box.space.T1.id, {}) end)
        t.assert_error_msg_contains(error_msg,
            function() box.schema.space.drop(box.space.T1.id) end)
        t.assert_error_msg_contains(error_msg,
            function() box.schema.index.create(box.space.T1.id, 'name') end)
        t.assert_error_msg_contains(error_msg,
            function() box.schema.index.alter(box.space.T1.id, 0, {}) end)
        t.assert_error_msg_contains(error_msg,
            function() box.schema.index.drop(box.space.T1.id, 0) end)
        t.assert_error_msg_contains(error_msg,
            function() box.schema.sequence.create('test') end)
        t.assert_error_msg_contains(error_msg,
            function() box.schema.sequence.alter('test', {}) end)
        t.assert_error_msg_contains(error_msg,
            function() box.schema.sequence.drop('test') end)
        t.assert_error_msg_contains(error_msg,
            function() box.schema.func.create('test') end)
        t.assert_error_msg_contains(error_msg,
            function() box.schema.func.drop('test') end)

        box.schema.upgrade()

        box.schema.space.create('test')
        box.schema.space.alter(box.space.test.id, {})
        box.schema.space.drop(box.space.test.id)
        box.schema.index.create(box.space.T1.id, 'name')
        box.schema.index.alter(box.space.T1.id, 1, {})
        box.schema.index.drop(box.space.T1.id, 1)
        box.schema.sequence.create('test')
        box.schema.sequence.alter('test', {})
        box.schema.sequence.drop('test')
        box.schema.func.create('test')
        box.schema.func.drop('test')
    end)
end
