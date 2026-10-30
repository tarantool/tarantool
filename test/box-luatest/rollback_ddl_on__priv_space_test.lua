local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        local t = box.schema.space.create('test')
        t:create_index('pk')
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Test that rollback of privilege grant works correctly.
g.test_rollback_grant = function(cg)
    cg.server:exec(function()
        box.schema.user.create('grant')
        local msg = "Read access to space 'test' is denied for user 'grant'"
        box.session.su('grant', function()
            t.assert_error_msg_content_equals(msg, function()
                box.space.test:select{}
            end)
        end)
        box.begin()
        box.schema.user.grant('grant', 'read', 'space', 'test')
        box.rollback()
        box.session.su('grant', function()
            t.assert_error_msg_content_equals(msg, function()
                box.space.test:select{}
            end)
        end)
    end)
end

-- Test that rollback of privilege revoke works correctly.
g.test_rollback_revoke = function(cg)
    cg.server:exec(function()
        box.schema.user.create('revoke')
        box.schema.user.grant('revoke', 'read', 'space', 'test')
        box.session.su('revoke', function()
            t.assert_equals({}, box.space.test:select{})
        end)
        box.begin()
        box.schema.user.revoke('revoke', 'read', 'space', 'test')
        box.rollback()
        box.session.su('revoke', function()
            t.assert_equals({}, box.space.test:select{})
        end)
    end)
end

-- Test that rollback of privilege modification works correctly.
g.test_rollback_modify = function(cg)
    cg.server:exec(function()
        box.schema.user.create('modify')
        box.schema.user.grant('modify', 'read', 'space', 'test')
        local msg = "Write access to space 'test' is denied for user 'modify'"
        box.session.su('modify', function()
            t.assert_equals({}, box.space.test:select{})
            t.assert_error_msg_content_equals(msg, function()
                box.space.test:delete{0}
            end)
        end)
        box.begin()
        box.schema.user.grant('modify', 'write', 'space', 'test')
        box.rollback()
        box.session.su('modify', function()
            t.assert_equals({}, box.space.test:select{})
            t.assert_error_msg_content_equals(msg, function()
                box.space.test:delete{0}
            end)
        end)
    end)
end
