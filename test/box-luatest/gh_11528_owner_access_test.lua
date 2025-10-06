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

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.s ~= nil then
            box.space.s:drop()
        end
        box.schema.user.drop('uu', {if_exists = true})
        box.schema.user.drop('u', {if_exists = true})
        box.schema.role.drop('r', {if_exists = true})
    end)
end)

-- Test that the dropped grants don't remain on the role.
g.test_dropped_grants = function(cg)
    cg.server:exec(function()
        -- Create a user and make it able to do DDL.
        box.schema.user.create('u')
        box.session.su('admin', box.schema.user.grant,
                       'u', 'read,write', 'universe')

        -- Create a role.
        box.schema.role.create('r')

        -- Allow and disallow the user to drop the role.
        box.schema.user.grant('u', 'drop', 'role', 'r')
        box.schema.user.revoke('u', 'drop', 'role', 'r')

        -- Drop the role by the user (this should be disallowed).
        t.assert_error_msg_equals(
            "Drop access to role 'r' is denied for user 'u'",
            box.session.su, 'u', box.schema.role.drop, 'r')

        t.assert(box.schema.role.exists('r'))
    end)
end

-- Test that it's possible to have both 'execute' and 'owner' access on a role.
g.test_multiple_role_accesses = function(cg)
    cg.server:exec(function()
        -- Create a space and a role that can alter the space and execute DDLs.
        box.session.su('admin', box.schema.space.create, 's')
        box.session.su('admin', box.schema.role.create, 'r')
        box.session.su('admin', box.schema.role.grant,
                       'r', 'alter', 'space', 's')
        box.session.su('admin', box.schema.role.grant,
                       'r', 'read,write,create', 'universe')

        -- Create a user and grant accesses to the role.
        box.schema.user.create('u')
        box.schema.user.grant('u', 'execute,owner', 'role', 'r')

        -- Check the user can alter the space (as the role allows).
        box.session.su('u', box.space.s.alter, box.space.s, {})

        -- Check the user can grant the role (as its owner).
        box.session.su('u', box.schema.user.create, 'uu')
        box.session.su('u', box.schema.user.grant, 'uu', 'r')
    end)
end

-- Test that the 'owner' access on a space works as expected.
g.test_space_owner = function(cg)
    cg.server:exec(function()
        -- Create a space and a user able to do DDLs tested.
        box.schema.space.create('s')
        box.schema.user.create('u')
        box.schema.user.grant('u', 'read,write', 'space', '_space')
        box.schema.user.grant('u', 'read,write', 'space', '_index')
        box.schema.user.grant('u', 'read,write', 'space', '_priv')
        box.schema.user.grant('u', 'read', 'space', '_space_sequence')
        box.schema.user.grant('u', 'read', 'space', '_trigger')
        box.schema.user.grant('u', 'read', 'space', '_func_index')
        box.schema.user.grant('u', 'read', 'space', '_truncate')

        -- Check DMLs and DDLs for the space object owner.
        -- Have to specify the 'create' access even for an
        -- owner to be able to create indexes for the space.
        box.schema.user.grant('u', 'owner,create', 'space', 's')
        box.session.su('u', box.space.s.create_index, box.space.s, 'pk')
        box.session.su('u', box.space.s.replace, box.space.s, {1})
        box.session.su('u', box.space.s.select, box.space.s)
        box.session.su('u', box.space.s.alter, box.space.s, {})
        -- We drop all the object privileges prior to space drop,
        -- including the new user's privilege to own the space.
        t.assert_error_msg_equals(
            "Drop access to space 's' is denied for user 'u'",
            box.session.su, 'u', box.space.s.drop, box.space.s)
        -- Recreate the space (also drops its accesses).
        box.space.s:drop()
        box.schema.space.create('s')

        -- Check DMLs and DDLs for the space entity owner.
        box.schema.user.grant('u', 'owner', 'space')
        box.schema.user.grant('u', 'create', 'space', 's')
        box.session.su('u', box.space.s.create_index, box.space.s, 'pk')
        box.session.su('u', box.space.s.replace, box.space.s, {1})
        box.session.su('u', box.space.s.select, box.space.s)
        box.session.su('u', box.space.s.alter, box.space.s, {})
        box.session.su('u', box.space.s.drop, box.space.s)
        box.schema.user.revoke('u', 'owner', 'space')
        box.schema.space.create('s')

        -- Check DMLs and DDLs for the space entity owner.
        box.schema.user.grant('u', 'owner', 'universe')
        box.schema.user.grant('u', 'create', 'space', 's')
        box.session.su('u', box.space.s.create_index, box.space.s, 'pk')
        box.session.su('u', box.space.s.replace, box.space.s, {1})
        box.session.su('u', box.space.s.select, box.space.s)
        box.session.su('u', box.space.s.alter, box.space.s, {})
        box.session.su('u', box.space.s.drop, box.space.s)
        box.schema.user.revoke('u', 'owner', 'universe')
    end)
end

-- Test that the 'owner' access on a function works as expected.
g.test_func_owner = function(cg)
    cg.server:exec(function()
        local func_create_opts = {
            is_sandboxed = true,
            is_deterministic = true,
            body = "function(t) return {t[1]} end",
        }

        -- Create a user able to do DDLs tested.
        box.schema.user.create('u')
        box.schema.user.grant('u', 'read,write', 'space', '_space')
        box.schema.user.grant('u', 'read,write', 'space', '_index')
        box.schema.user.grant('u', 'read,write', 'space', '_priv')
        box.schema.user.grant('u', 'read,write', 'space', '_func')
        box.schema.user.grant('u', 'read,write', 'space', '_func_index')
        box.schema.user.grant('u', 'read', 'space', '_space_sequence')
        box.schema.user.grant('u', 'read', 'space', '_truncate')
        box.schema.user.grant('u', 'read', 'space', '_trigger')
        box.schema.user.grant('u', 'create', 'space')

        -- Use and drop a function by its owner.
        box.schema.func.create('f', func_create_opts)
        box.schema.user.grant('u', 'owner', 'function', 'f')
        box.session.su('u', t.assert, box.schema.func.exists, 'f')
        box.session.su('u', box.schema.space.create, 's')
        box.session.su('u', box.space.s.create_index, box.space.s, 'pk')
        box.session.su('u', box.space.s.create_index,
                       box.space.s, 'fk', {func = 'f'})
        box.session.su('u', box.space.s.insert, box.space.s, {1})
        box.session.su('u', box.space.s.insert, box.space.s, {3})
        box.session.su('u', box.space.s.insert, box.space.s, {2})
        t.assert_equals(box.space.s:select(), {{1}, {2}, {3}})
        box.session.su('u', box.space.s.drop, box.space.s)
        -- We drop all the object privileges prior to function drop,
        -- including the new user's privilege to own the function.
        t.assert_error_msg_equals(
            "Drop access to function 'f' is denied for user 'u'",
            box.session.su, 'u', box.func.f.drop, box.func.f)
        box.schema.user.revoke('u', 'owner', 'function', 'f')

        -- Use and drop a function by entity owner.
        box.schema.user.grant('u', 'owner', 'function')
        box.session.su('u', t.assert, box.schema.func.exists, 'f')
        box.session.su('u', box.schema.space.create, 's')
        box.session.su('u', box.space.s.create_index, box.space.s, 'pk')
        box.session.su('u', box.space.s.create_index,
                       box.space.s, 'fk', {func = 'f'})
        box.session.su('u', box.space.s.insert, box.space.s, {1})
        box.session.su('u', box.space.s.insert, box.space.s, {3})
        box.session.su('u', box.space.s.insert, box.space.s, {2})
        t.assert_equals(box.space.s:select(), {{1}, {2}, {3}})
        box.session.su('u', box.space.s.drop, box.space.s)
        box.session.su('u', box.func.f.drop, box.func.f)
        box.schema.user.revoke('u', 'owner', 'function')

        -- Use and drop a function by the universe owner.
        box.schema.func.create('f', func_create_opts)
        box.schema.user.grant('u', 'owner', 'universe')
        box.session.su('u', t.assert, box.schema.func.exists, 'f')
        box.session.su('u', box.schema.space.create, 's')
        box.session.su('u', box.space.s.create_index, box.space.s, 'pk')
        box.session.su('u', box.space.s.create_index,
                       box.space.s, 'fk', {func = 'f'})
        box.session.su('u', box.space.s.insert, box.space.s, {1})
        box.session.su('u', box.space.s.insert, box.space.s, {3})
        box.session.su('u', box.space.s.insert, box.space.s, {2})
        t.assert_equals(box.space.s.index.fk:select(), {{1}, {2}, {3}})
        box.session.su('u', box.space.s.drop, box.space.s)
        box.session.su('u', box.func.f.drop, box.func.f)
        box.schema.user.revoke('u', 'owner', 'universe')
    end)
end

-- Test that the 'owner' access on a user works as expected.
g.test_user_owner = function(cg)
    cg.server:exec(function()
        -- TODO: grant access for owned user to owned space.
        -- Create a user able to do the DDLs for the test.
        box.schema.user.create('u')
        box.schema.user.grant('u', 'read,write', 'space', '_user')
        box.schema.user.grant('u', 'write', 'space', '_priv')
        box.schema.user.grant('u', 'create', 'user')
        box.schema.user.grant('u', 'create', 'role')

        -- Alter and drop the user by its owner.
        box.schema.user.create('uu')
        box.schema.user.grant('u', 'owner', 'user', 'uu')
        box.session.su('u', box.schema.role.create, 'r')
        box.session.su('u', box.schema.user.grant, 'uu', 'r')
        box.session.su('u', box.schema.user.revoke, 'uu', 'r')
        box.session.su('u', box.schema.user.grant, 'uu', 'r')
        -- We drop all the object privileges prior to user drop,
        -- including the new user's privilege to own the user.
        -- So he can't proceed revoking the object's privileges.
        t.assert_error_msg_equals(
            "Revoke access to user 'uu' is denied for user 'u'",
            box.session.su, 'u', box.schema.user.drop, 'uu')
        box.session.su('u', box.schema.role.drop, 'r')
        box.schema.user.drop('uu')

        -- Alter and drop the user by entity owner.
        box.schema.user.create('uu')
        box.schema.user.grant('u', 'owner', 'user')
        box.session.su('u', box.schema.role.create, 'r')
        box.session.su('u', box.schema.user.grant, 'uu', 'r')
        box.session.su('u', box.schema.user.revoke, 'uu', 'r')
        box.session.su('u', box.schema.user.grant, 'uu', 'r')
        box.session.su('u', box.schema.user.drop, 'uu')
        box.session.su('u', box.schema.role.drop, 'r')
        box.schema.user.revoke('u', 'owner', 'user')

        -- Alter and drop the user by universe owner.
        box.schema.user.create('uu')
        box.schema.user.grant('u', 'owner', 'universe')
        box.session.su('u', box.schema.role.create, 'r')
        box.session.su('u', box.schema.user.grant, 'uu', 'r')
        box.session.su('u', box.schema.user.revoke, 'uu', 'r')
        box.session.su('u', box.schema.user.grant, 'uu', 'r')
        box.session.su('u', box.schema.user.drop, 'uu')
        box.session.su('u', box.schema.role.drop, 'r')
        box.schema.user.revoke('u', 'owner', 'universe')
    end)
end

-- Test that the 'owner' access on a role works as expected.
g.test_role_owner = function(cg)
    cg.server:exec(function()
        -- Create a user able to do the DDLs for the test.
        box.schema.user.create('u')
        box.schema.user.grant('u', 'read,write', 'space', '_user')
        box.schema.user.grant('u', 'write', 'space', '_priv')
        box.schema.user.grant('u', 'create', 'user')

        -- A role owner can grant and revoke it even
        -- if the role was created by another user.
        box.schema.role.create('r')
        box.schema.user.grant('u', 'owner', 'role', 'r')
        box.session.su('u', box.schema.user.create, 'uu')
        box.session.su('u', box.schema.user.grant, 'uu', 'r')
        box.session.su('u', box.schema.user.revoke, 'uu', 'r')
        box.session.su('u', box.schema.user.drop, 'uu')
        -- Can't drop it though, as we revoke all privileges
        -- on the object prior, including the new owner's
        -- privilege to own the object and thus, to drop it.
        t.assert_error_msg_equals(
            "Drop access to role 'r' is denied for user 'u'",
            box.session.su, 'u', box.schema.role.drop, 'r')
        t.assert(box.schema.role.exists('r'))
        -- The drop is atomic, so the dropped privs must be restored.
        box.schema.user.revoke('u', 'owner', 'role', 'r')

        -- A role entity owner can grant, revoke and drop any role.
        box.schema.user.grant('u', 'owner', 'role')
        box.session.su('u', box.schema.user.create, 'uu')
        box.session.su('u', box.schema.user.grant, 'uu', 'r')
        box.session.su('u', box.schema.user.revoke, 'uu', 'r')
        box.session.su('u', box.schema.user.drop, 'uu')
        box.session.su('u', box.schema.role.drop, 'r')
        t.assert(not box.schema.role.exists('r'))
        box.schema.user.revoke('u', 'owner', 'role')

        -- A universe owner can grant, revoke and drop any role.
        box.schema.role.create('r')
        box.schema.user.grant('u', 'owner', 'universe')
        box.session.su('u', box.schema.user.create, 'uu')
        box.session.su('u', box.schema.user.grant, 'uu', 'r')
        box.session.su('u', box.schema.user.revoke, 'uu', 'r')
        box.session.su('u', box.schema.user.drop, 'uu')
        box.session.su('u', box.schema.role.drop, 'r')
        t.assert(not box.schema.role.exists('r'))
        box.schema.user.revoke('u', 'owner', 'universe')

        -- Having none of these accesses prihibits grant, revoke
        -- and drop of the role not created by oneself.
        box.schema.role.create('r')
        box.session.su('u', box.schema.user.create, 'uu')
        t.assert_error_msg_content_equals(
            "Grant access to role 'r' is denied for user 'u'",
            box.session.su, 'u', box.schema.user.grant, 'uu', 'r')
        box.schema.user.grant('uu', 'r')
        t.assert_error_msg_content_equals(
            "User 'uu' does not have role 'r'",
            box.session.su, 'u', box.schema.user.revoke, 'uu', 'r')
        box.schema.user.grant('u', 'read', 'space', '_priv')
        t.assert_error_msg_content_equals(
            "Revoke access to role 'r' is denied for user 'u'",
            box.session.su, 'u', box.schema.user.revoke, 'uu', 'r')
        box.session.su('u', box.schema.user.drop, 'uu')
        t.assert_error_msg_content_equals(
            "Drop access to role 'r' is denied for user 'u'",
            box.session.su, 'u', box.schema.role.drop, 'r')
    end)
end

g.test_sequence_owner = function(cg)
    cg.server:exec(function()
        -- Create a user able to do DDLs tested.
        box.schema.user.create('u')
        box.schema.user.grant('u', 'read,write', 'space', '_space')
        box.schema.user.grant('u', 'read,write', 'space', '_index')
        box.schema.user.grant('u', 'read,write', 'space', '_priv')
        box.schema.user.grant('u', 'read,write', 'space', '_space_sequence')
        box.schema.user.grant('u', 'write', 'space', '_sequence')
        box.schema.user.grant('u', 'write', 'space', '_sequence_data')
        box.schema.user.grant('u', 'read', 'space', '_trigger')
        box.schema.user.grant('u', 'read', 'space', '_func_index')
        box.schema.user.grant('u', 'read', 'space', '_truncate')
        box.schema.user.grant('u', 'create', 'space')

        -- Use the sequence by its owner.
        box.schema.sequence.create('seq')
        box.schema.user.grant('u', 'owner', 'sequence', 'seq')
        box.session.su('u', box.schema.space.create, 's')
        box.session.su('u', box.space.s.create_index, box.space.s,
                       'pk', {sequence = box.sequence.seq.id})
        box.session.su('u', box.space.s.insert, box.space.s, {box.NULL})
        box.session.su('u', box.space.s.insert, box.space.s, {box.NULL})
        box.session.su('u', box.sequence.seq.set, box.sequence.seq, 99)
        box.session.su('u', box.space.s.insert, box.space.s, {box.NULL})
        box.session.su('u', box.sequence.seq.alter, box.sequence.seq,
                       {step = 2, name = 'qes'})
        box.session.su('u', box.space.s.insert, box.space.s, {box.NULL})
        t.assert_equals(box.space.s:select(), {{1}, {2}, {100}, {102}})
        box.session.su('u', box.space.s.drop, box.space.s)
        -- We drop all the object privileges prior to sequence drop,
        -- including the new user's privilege to own the sequence.
        t.assert_error_msg_equals(
            "Drop access to sequence 'qes' is denied for user 'u'",
            box.session.su, 'u', box.sequence.qes.drop, box.sequence.qes)
        -- Drops the privileges too.
        box.sequence.qes:drop()

        -- Use the sequence by entity owner.
        box.schema.sequence.create('seq')
        box.schema.user.grant('u', 'owner', 'sequence')
        box.session.su('u', box.schema.space.create, 's')
        box.session.su('u', box.space.s.create_index, box.space.s,
                       'pk', {sequence = box.sequence.seq.id})
        box.session.su('u', box.space.s.insert, box.space.s, {box.NULL})
        box.session.su('u', box.space.s.insert, box.space.s, {box.NULL})
        box.session.su('u', box.sequence.seq.set, box.sequence.seq, 99)
        box.session.su('u', box.space.s.insert, box.space.s, {box.NULL})
        box.session.su('u', box.sequence.seq.alter, box.sequence.seq,
                       {step = 2, name = 'qes'})
        box.session.su('u', box.space.s.insert, box.space.s, {box.NULL})
        t.assert_equals(box.space.s:select(), {{1}, {2}, {100}, {102}})
        box.session.su('u', box.space.s.drop, box.space.s)
        box.session.su('u', box.sequence.qes.drop, box.sequence.qes)
        box.schema.user.revoke('u', 'owner', 'sequence')

        -- Use the sequence by the universe owner.
        box.schema.sequence.create('seq')
        box.schema.user.grant('u', 'owner', 'universe')
        box.session.su('u', box.schema.space.create, 's')
        box.session.su('u', box.space.s.create_index, box.space.s,
                       'pk', {sequence = box.sequence.seq.id})
        box.session.su('u', box.space.s.insert, box.space.s, {box.NULL})
        box.session.su('u', box.space.s.insert, box.space.s, {box.NULL})
        box.session.su('u', box.sequence.seq.set, box.sequence.seq, 99)
        box.session.su('u', box.space.s.insert, box.space.s, {box.NULL})
        box.session.su('u', box.sequence.seq.alter, box.sequence.seq,
                       {step = 2, name = 'qes'})
        box.session.su('u', box.space.s.insert, box.space.s, {box.NULL})
        t.assert_equals(box.space.s:select(), {{1}, {2}, {100}, {102}})
        box.session.su('u', box.space.s.drop, box.space.s)
        box.session.su('u', box.sequence.qes.drop, box.sequence.qes)
        box.schema.user.revoke('u', 'owner', 'universe')
    end)
end
