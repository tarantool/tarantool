local t = require('luatest')
local server = require('luatest.server')

local g = t.group('gh-7149')

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
    g.server = server:new{
        alias = 'master',
        datadir = 'test/box-luatest/upgrade/2.10.4',
    }
    g.server:start()
end)

g.test_ddl_ops = function()
    g.server:exec(function()
        -- DDL with old schema is forbidden.
        local errmsg = 'Your schema version is 2.10.4 while Tarantool ' ..
                       box.info.version ..
                       ' requires a more recent schema version. ' ..
                       'Please, consider using box.schema.upgrade().'
        t.assert_error_msg_equals(errmsg, box.schema.user.create, 'gh_7149')
        t.assert_error_msg_equals(errmsg, box.schema.role.create, 'gh_7149')
        t.assert_error_msg_equals(errmsg, box.schema.func.create, 'gh_7149')
        t.assert_error_msg_equals(errmsg, box.schema.space.create, 'gh_7149')
        t.assert_error_msg_equals(errmsg, box.schema.sequence.create, 'gh_7149')
        t.assert_error_msg_equals(errmsg, box.schema.user.drop, 'eve')
        t.assert_error_msg_equals(errmsg, box.schema.role.drop, 'test')
        t.assert_error_msg_equals(errmsg, box.session.su, 'admin',
                                  box.schema.user.grant, 'eve', 'super')
        t.assert_error_msg_equals(errmsg, box.schema.func.drop, 'test')
        t.assert_error_msg_equals(errmsg, box.schema.sequence.drop, 'test')
        t.assert_error_msg_equals(errmsg, box.space.test.drop, box.space.test)
        t.assert_error_msg_equals(errmsg, box.space.test.create_index,
                                  box.space.test, 'gh_7149')
        t.assert_error_msg_equals(errmsg, box.space.test.index.pk.drop,
                                  box.space.test.index.pk)

        -- But box.internal.run_schema_upgrade can be used to get round.
        box.internal.run_schema_upgrade(box.schema.create_space, 'gh_7149')
        box.internal.run_schema_upgrade(box.space.gh_7149.drop,
                                        box.space.gh_7149)

        -- Check that errors are forwarded.
        t.assert_error_msg_equals('foo', box.internal.run_schema_upgrade,
                                  box.error, {reason = 'foo'})

        box.schema.upgrade()

        -- After upgrade DDL should work fine.
        local s = box.schema.space.create('gh_7149')
        s:format({{'id', 'unsigned'}})
        s:create_index('pk', {sequence = true})
        s:drop()
        box.schema.func.create('gh_7149')
        box.schema.func.drop('gh_7149')
        box.schema.user.create('gh_7149')
        box.session.su('admin', box.schema.user.grant, 'gh_7149', 'super')
        box.schema.user.drop('gh_7149')
    end)
end

g.before_test('test_concurrent_upgrade', function()
    g.server = server:new{
        alias = 'master',
        datadir = 'test/box-luatest/upgrade/2.10.4',
    }
    g.server:start()
end)

g.test_concurrent_upgrade = function()
    g.server:exec(function()
        local fiber = require('fiber')
        local c1 = fiber.channel()
        local c2 = fiber.channel()
        local f = fiber.new(box.internal.run_schema_upgrade, function()
            c1:put(true)
            t.assert(c2:get())
        end)
        f:set_joinable(true)
        t.assert(c1:get())
        t.assert_error_msg_equals('Schema upgrade is already in progress',
                                  box.schema.upgrade)
        c2:put(true)
        t.assert(f:join())
        box.schema.upgrade()
    end)
end
