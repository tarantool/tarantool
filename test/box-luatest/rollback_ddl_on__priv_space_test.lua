local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Test that rollback of DDL statements on the `_priv` space works correctly.
g.test_rollback_ddl_on__priv_space = function(cg)
    cg.server:exec(function()
        box.schema.user.create('grant')
        box.schema.user.create('revoke')
        box.schema.user.create('modify')

        box.session.su('grant')
        local grant_user = box.session.uid()
        box.session.su('revoke')
        local revoke_user = box.session.uid()
        box.session.su('modify')
        local modify_user = box.session.uid()

        box.session.su('admin')

        local grant_privs = box.space._priv:select{grant_user}
        box.begin()
        box.schema.user.grant('grant', 'execute', 'universe')
        box.rollback()
        t.assert_equals(box.space._priv:select{grant_user}, grant_privs)

        box.schema.user.grant('revoke', 'execute', 'universe')
        local revoke_privs = box.space._priv:select{revoke_user}
        box.begin()
        box.schema.user.revoke('revoke', 'execute', 'universe')
        box.rollback()
        t.assert_equals(box.space._priv:select{revoke_user}, revoke_privs)

        box.schema.user.grant('modify', 'read,write,execute', 'universe')
        local modify_privs = box.space._priv:select{modify_user}
        box.begin()
        box.schema.user.revoke('modify', 'execute', 'universe')
        box.rollback()
        t.assert_equals(box.space._priv:select{modify_user}, modify_privs)
    end)
end
