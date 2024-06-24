local server = require('luatest.server')
local t = require('luatest')

local g1 = t.group('generic')
local g2 = t.group('two_way', {{flag = true}, {flag = false}})

for _, g in pairs({g1, g2}) do
    g.before_all(function(cg)
        cg.server = server:new()
        cg.server:start()
    end)

    g.after_all(function(cg)
        cg.server:drop()
    end)

    g.after_each(function(cg)
        cg.server:exec(function()
            if box.is_in_txn() then
                box.rollback()
            end
            if box.space.s ~= nil then
                box.space.s:drop()
            end
            if box.space.s2 ~= nil then
                box.space.s2:drop()
            end
            box.schema.user.drop('new_user', {if_exists = true})
            box.schema.sequence.drop('seq', {if_exists = true})
            box.schema.func.drop('func', {if_exists = true})
            box.schema.func.drop('constraint', {if_exists = true})
            box.schema.func.drop('field_constraint_1', {if_exists = true})
            box.schema.func.drop('field_constraint_2', {if_exists = true})
        end)
    end)
end

-- Test a DDL transaction which creates a space, formats it to have an integer
-- column, fills it with some data, changes the format to 'any', changes the
-- data values to 'string', and changes the format to 'string'.
g1.test_swap_space_names = function(cg)
    cg.server:exec(function()
        box.begin()

        local s = box.schema.space.create('s')

        s:create_index('pk', {parts = {1, 'scalar'}})
        s:format({{name = 'id', type = 'integer'}})

        for i = 1, 5 do
            s:insert({i})
        end

        s:format({{name = 'id', type = 'any'}})

        for i = 1, 5 do
            s:delete({i})
        end

        s:format({{name = 'id', type = 'string'}})

        s:insert({'a'})
        s:insert({'b'})
        s:insert({'c'})

        box.commit()

        t.assert_equals(s:select(), {{'a'}, {'b'}, {'c'}})
        t.assert_equals(s:format()[1].type, 'string')
    end)
end

-- Create a space, user, grant privileges to the user to use the new space
-- and access the space on behalf of the new user - all in a transaction.
g1.test_space_create_user_grant_use = function(cg)
    cg.server:exec(function()
        box.begin()

        local first_user = box.session.effective_user()
        local s = box.schema.space.create('s')

        s:create_index('pk', {parts = {1, 'string'}})
        s:insert({first_user})

        box.schema.user.create('new_user')
        box.schema.user.grant('new_user', 'read,write,usage', 'space', 's')
        box.session.su('new_user')

        s:insert({'new_user'})
        box.session.su(first_user)

        box.commit()

        t.assert_equals(s:select(), {{first_user}, {'new_user'}})
    end)
end

-- Create a space and secondary indexes in a transaction.
g1.test_create_space_and_indexes = function(cg)
    cg.server:exec(function()
        box.begin()

        local s = box.schema.space.create('s')
        local pk = s:create_index('pk', {parts = {1, 'unsigned'}})
        local sk = s:create_index('sk', {parts = {2, 'unsigned'}})

        s:insert({1, 3})
        s:insert({2, 2})
        s:insert({3, 1})

        box.commit()

        t.assert_not_equals(s.index.pk, nil)
        t.assert_not_equals(s.index.sk, nil)
        t.assert_equals(pk:select(), {{1, 3}, {2, 2}, {3, 1}})
        t.assert_equals(sk:select(), {{3, 1}, {2, 2}, {1, 3}})
    end)
end

-- Create a space with a sequence.
g1.test_create_space_and_sequence = function(cg)
    cg.server:exec(function()
        box.begin()

        local s = box.schema.space.create('s')
        box.schema.sequence.create('seq', {max = 42, step = -1})
        s:create_index('pk', {sequence = 'seq'})

        s:insert({nil, 1})
        s:insert({nil, 2})

        box.commit()

        s:insert({nil, 3})
        s:insert({nil, 4})

        t.assert_not_equals(s.index.pk, nil)
        t.assert_equals(s:select(), {{39, 4}, {40, 3}, {41, 2}, {42, 1}})
    end)
end

-- Test a transaction which sets the on_rollback trigger and creates a space,
-- the space triggers, inserts a bunch of data and then commits or rolls back.
g2.test_create_space_and_triggers = function(cg)
    cg.server:exec(function(commit)
        local on_rollback_fired = false
        local on_replace_assert_log = ''

        box.begin()

        box.on_rollback(function ()
            on_rollback_fired = true
        end)

        local s = box.schema.space.create('s')

        for _, trigger in pairs(s:on_replace()) do
            -- Drop the old on_replace triggers. This is required because the
            -- triggers are bound to space IDs, and they're not deleted on the
            -- space drop.
            s:on_replace(nil, trigger)
        end

        s:on_replace(function(old, new, space, op)
            -- Can't raise errors here, so let's just log them in a variable.
            local function on_replace_assert_equals(a, b)
                if a ~= b then
                    on_replace_assert_log = on_replace_assert_log ..
                        'Assertion failed in the on_replace trigger: expected '
                        .. tostring(b) .. ', got ' .. tostring(a) .. '.\n'
                end
            end

            on_replace_assert_equals(old, nil)
            on_replace_assert_equals(#new, 2)
            on_replace_assert_equals(space, s.name)
            on_replace_assert_equals(op, 'INSERT')
        end)

        s:create_index('pk')

        s:insert({1, 1})
        s:insert({2, 2})
        s:insert({3, 3})

        if commit then
            box.commit()
            t.assert_equals(on_rollback_fired, false)
            t.assert_not_equals(box.space.s, nil)
            t.assert_equals(s:select(), {{1, 1}, {2, 2}, {3, 3}})
        else
            box.rollback()
            t.assert_equals(on_rollback_fired, true)
            t.assert_equals(box.space.s, nil)
        end
        t.assert_equals(on_replace_assert_log, '')
    end, {cg.params.flag})
end

-- Drop a[n empty] space with stuff.
g2.test_drop_optionally_empty_space = function(cg)
    cg.server:exec(function(empty)
        assert(empty == true or empty == false)

        box.begin()

        local s = box.schema.space.create('s')
        local s2 = box.schema.space.create('s2')

        box.schema.func.create('func', {
            body = 'function (tuple) return {tuple[1]} end',
            is_deterministic = true,
            is_sandboxed = true
        })

        box.schema.func.create('constraint', {
            body = 'function(t, c) return t ~= nil and #t ~= 1234 end',
            is_deterministic = true,
            is_sandboxed = true
        })

        box.schema.func.create('field_constraint_1', {
            body = 'function(f, c) return f ~= nil and f ~= 1234 end',
            is_deterministic = true,
            is_sandboxed = true
        })

        box.schema.func.create('field_constraint_2', {
            body = 'function(f, c) return f ~= nil and f ~= 4321 end',
            is_deterministic = true,
            is_sandboxed = true
        })

        box.schema.sequence.create('seq', {max = 42, step = -1})
        s:create_index('pk', {sequence = 'seq'})
        s:create_index('sk', {parts = {2, 'unsigned'}})
        s:create_index('fk', {parts = {1, 'unsigned'}, func='func'})
        s2:create_index('pk')

        box.schema.space.alter(s.id, {
            foreign_key = {
                f1 = {space = s2.name, field = {[2] = 1}},
            },
            constraint = 'constraint',
            format = {
                {name = 'id', type = 'any', constraint = 'field_constraint_1'},
                {name = 'fk', type = 'any', constraint = 'field_constraint_2'},
            },
        })

        if not empty then
            for i = 1, 1000 do
                s2:insert({i + 1})
                s:insert({i, i + 1})
            end
        end

        box.commit()

        s:drop()
    end, {cg.params.flag})
end
