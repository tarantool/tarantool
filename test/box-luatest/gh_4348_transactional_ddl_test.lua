local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-4348-transactional-ddl-test',
    t.helpers.matrix({mvcc = {true, false}}))

g.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {memtx_use_mvcc_engine = cg.params.mvcc}
    })
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
        if box.space.s2 ~= nil then
            box.space.s2:drop()
        end
        box.schema.func.drop('f', {if_exists = true})
        box.schema.sequence.drop('seq', {if_exists = true})
        box.schema.user.drop('new_user', {if_exists = true})
        box.schema.user.drop('new_user_2', {if_exists = true})
    end)
end)

g.test_atomic_space_drop = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('s')
        local s2 = box.schema.space.create('s2')

        s:create_index('pk')
        s2:create_index('pk')

        local s2sk = s2:create_index('sk')

        s2:insert({2})
        s:insert({1, 2})

        box.schema.space.alter(s.id, {foreign_key = {
            f1 = {space = s2.name, field = {[2] = 1}},
        }})

        -- Attempt tp drop a referenced foreign key space with secondary
        -- indexes.
        --
        -- Currently the space drop flow looks like this:
        -- 1. Drop automatically generated sequence for the space.
        -- 2. Drop triggers of the space.
        -- 3. Disable functional indexes of the space.
        -- 4. (!) Remove each index of the space starting from secondary
        --    indexes.
        -- 5. Revoke the space privileges.
        -- 6. Remove the associated entry from the _truncate system space.
        -- 7. Remove the space entry from _space system space.
        --
        -- If the space is referenced by another space with foreign key
        -- constraint then the flow fails on the primary index drop (step 4).
        -- But at that point all the secondary indexes are dropped already, so
        -- we have an inconsistent state of the database.
        --
        -- But if the drop function is transactional then the dropped secondary
        -- indexes are restored on transaction revert and the database remains
        -- consistent: we can continue using the secondary index of the table we
        -- have failed to drop.

        local err = "Can't modify space '" .. s2.name
                    .. "': space is referenced by foreign key"
        t.assert_error_msg_equals(err, s2.drop, s2)

        -- The secondary index is restored on drop fail so this must succeed.
        s2sk:select(42)
    end)
end


g.test_atomic_index_create = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('s')
        s:create_index('pk', {parts = {{1, 'scalar'}}})

        -- Currently on the index creation _func_index entry is created after
        -- the _index space is modified, so if the error is thrown on performing
        -- function ID check, the new index is registered already.
        --
        -- If the index creation is not transactional then we gonna have a new
        -- disabled functional index. But if it is then the index creation is
        -- rolled back.

        local err = "Function '42' does not exist"
        t.assert_error_msg_equals(err, s.create_index, s, 'fk', {func = 42})

        -- The created index should be dropped if the index creation is atomic.
        t.assert_equals(box.space.s.index.fk, nil)
    end)
end

-- The index drop is not testable without error injection.

g.test_atomic_index_alter = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('s')
        s:create_index('pk', {parts = {{1, 'scalar'}}})
        local sk = s:create_index('sk')

        -- Currently on the index alter _func_index entry is created after the
        -- _index space is modified, so if the error is thrown on performing
        -- function ID check, the index is altered already.
        --
        -- If the index creation is not transactional then we gonna have a
        -- disabled functional index. But if it is then the index alter is
        -- rolled back.

        local err = "Function '42' does not exist"
        t.assert_error_msg_equals(err, sk.alter, sk, {name = 'fk', func = 42})

        -- The index name should be reverted if the index alter is atomic.
        t.assert_equals(box.space.s.index[1].name, 'sk')
    end)
end

g.test_atomic_sequence_drop = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('s')
        s:create_index('pk', {sequence = true})

        -- The sequence drop routine first drops the corresponding entry from
        -- the _sequence_data space and then drops the sequence itself. So
        -- if the routine is not atomic we will observe the sequence reset
        -- on failed attempt to drop it. If it's not - the sequence data will
        -- be restored.

        -- Create an entry in the _sequence_data.
        box.space._sequence_data:insert({1, 42})

        -- Attempt to drop the sequence which is in use by the 's' space.
        local err = "Can't drop sequence 's_seq': the sequence is in use"
        t.assert_error_msg_equals(err, box.schema.sequence.drop, 's_seq')

        -- The dropped _sequence_data entry should be restored.
        t.assert_equals(box.space._sequence_data:select(), {{1, 42}})

        -- A new item should have the correct ID (which is 43).
        t.assert_equals(s:insert({nil, 1})[1], 43)
    end)
end

g.test_atomic_func_drop = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('s')
        s:create_index('pk')

        box.schema.func.create('f', {
            language = 'LUA',
            is_deterministic = true,
            is_sandboxed = true,
            body = 'function() return {1} end'
        })

        -- Use the function in a functional index.
        s:create_index('fk', {func = 'f'})

        -- Add an entry to the _priv space.
        box.schema.user.grant('admin', 'execute', 'function', 'f')

        -- Attempt to drop the function which is in use.
        local err = "function has references"
        t.assert_error_msg_contains(err, box.schema.func.drop, 'f')

        -- Check if the dropped privileges had been restored. If it's not the
        -- case then the following privilege revocation will fail.
        box.schema.user.revoke('admin', 'execute', 'function', 'f')
    end)
end

g.test_atomic_user_create = function(cg)
    cg.server:exec(function()
        -- Nothing should be there yet.
        t.assert_equals(box.space.s, nil)
        t.assert_equals(box.schema.user.exists('new_user'), false)

        -- The user creation procedure includes creation of an entry in the
        -- _user space and granting him some privileges. In case if the user
        -- creator does not have rights to grant privilegies to other users
        -- then the privilege granting part will fail.
        --
        -- In order to check that the creation is atomic, we create a user
        -- unable to grant ay privileges to any other user and so we make
        -- the user creation fail on privilege grant part. The created user
        -- should be dropped on the transaction rollback.

        local original_user = box.session.euid()

        -- Create a new user that can't grant any privileges.
        box.session.su('admin')
        box.schema.user.create('new_user')
        box.schema.user.grant('new_user', 'read,write', 'space', '_user')
        box.schema.user.grant('new_user', 'create', 'user')

        -- Attempt to create a used by this user will fail on privilege
        -- assignment stage, because he can't access the _priv space.
        box.session.su('new_user')
        local err = "Write access to space '_priv' is denied for user"
        t.assert_error_msg_contains(err, box.schema.user.create, 'new_user_2')

        box.session.su(original_user)

        -- Check if the new user had been dropped after transaction rollback.
        t.assert_equals(box.schema.user.exists('new_user_2'), false)
    end)
end

-- box.schema.role.drop is a variation of box.schema.user.drop.
g.test_atomic_user_drop = function(cg)
    cg.server:exec(function()
        -- Nothing should be there yet.
        t.assert_equals(box.space.s, nil)
        t.assert_equals(box.sequence.seq, nil)
        t.assert_equals(box.schema.user.exists('new_user'), false)

        -- The user drop is not just removal of an entry from the _user space.
        -- All the objects created by the user should be dropped too, including
        -- (but not limited to) spaces and sequences.
        --
        -- The spaces are dropped first, and, at some point later, we drop the
        -- user's sequences. That means in case if the user drop is not atomic,
        -- then it's possible the situation when we succeed to drop all spaces,
        -- but failed to drop a sequence. In this case we are going to have an
        -- inconsistent state: the changes are partially applied.
        --
        -- So here we create a user, make him create a space and a sequence, and
        -- then, in order to make the sequence drop fail, we use the sequence in
        -- an admin's space. So attempt drop a user will cause the sequence drop
        -- which will fail, because the sequence is in use by admin's space.
        --
        -- In order to check that the drop is atomic, we check if the space that
        -- had been dropped within the transaction, will still be there once the
        -- transaction is rolled back on failure.

        -- Create a new user and make him create a new space and sequence.
        local original_user = box.session.euid()
        box.session.su('admin')
        box.schema.user.create('new_user')
        box.schema.user.grant('new_user',
                              'create,drop,read,write,execute', 'universe')
        box.session.su('new_user', box.schema.space.create, 's')
        box.session.su('new_user', box.schema.sequence.create, 'seq')
        box.session.su(original_user)

        -- Use the sequence created by the new user.
        local s2 = box.schema.space.create('s2')
        s2:create_index('pk', {sequence = 'seq'})

        -- Attempt to drop the user will fail, because it drops everything that
        -- had been created by the user, including sequences. But the sequence
        -- created by the user is used by admin in his space.
        local err = "Can't drop sequence 'seq': the sequence is in use"
        t.assert_error_msg_equals(err, box.schema.user.drop, 'new_user')

        -- The sequence drop attempt happens after drop of all user spaces, but
        -- the dropped space should be recovered by the rollback if the space
        -- drop is transactional.
        t.assert_not_equals(box.space.s, nil)
    end)
end
