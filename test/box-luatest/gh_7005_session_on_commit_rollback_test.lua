local misc = require('test.luatest_helpers.misc')
local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.before_all = function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.schema.space.create('T')
        box.space.T:create_index('primary')
        box.schema.user.create('eve')
        box.schema.user.grant('eve', 'write', 'space', 'T')
    end)
end

g.after_all = function()
    g.server:drop()
end

-- Checks session and user in box.on_commit trigger callback.
g.test_session_on_commit = function()
    g.server:exec(function()
        local t = require('luatest')
        box.session.su('eve', function()
            t.assert_equals(box.session.effective_user(), 'eve')
            local id, user
            box.begin()
            box.on_commit(function()
                id = box.session.id()
                user = box.session.effective_user()
            end)
            box.space.T:replace{1}
            box.commit()
            t.assert_equals(id, box.session.id())
            t.assert_equals(user, box.session.effective_user())
        end)
    end)
end

-- Checks session and user in box.on_rollback trigger callback in case
-- transaction is rolled back with box.rollback().
g.test_session_on_graceful_rollback = function()
    g.server:exec(function()
        local t = require('luatest')
        box.session.su('eve', function()
            t.assert_equals(box.session.effective_user(), 'eve')
            local id, user
            box.begin()
            box.on_rollback(function()
                id = box.session.id()
                user = box.session.effective_user()
            end)
            box.space.T:replace{1}
            box.rollback()
            t.assert_equals(id, box.session.id())
            t.assert_equals(user, box.session.effective_user())
            t.assert_equals(user, 'eve')
        end)
    end)
end

-- Checks session and user in box.on_rollback trigger callback in case
-- transaction is rolled back on WAL error.
g.test_session_on_wal_error_rollback = function()
    misc.skip_if_not_debug()
    g.server:exec(function()
        local t = require('luatest')
        box.session.su('eve', function()
            t.assert_equals(box.session.effective_user(), 'eve')
            local id, user
            box.begin()
            box.on_rollback(function()
                id = box.session.id()
                user = box.session.effective_user()
            end)
            box.space.T:replace{1}
            box.error.injection.set('ERRINJ_WAL_WRITE', true)
            t.assert_error_msg_equals("Failed to write to disk", box.commit)
            box.error.injection.set('ERRINJ_WAL_WRITE', false)
            t.assert_equals(id, box.session.id())
            t.assert_equals(user, box.session.effective_user())
            t.assert_equals(user, 'eve')
        end)
    end)
end

g.after_test('test_session_on_wal_error_rollback', function()
    g.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_WRITE', false)
    end)
end)
