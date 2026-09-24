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
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_invalid_usage = function(cg)
    cg.server:exec(function()
        local _space = box.space._space
        local ADMIN = 1

        -- Can't change fields of a system space tuple which are a part of
        -- its primary key.
        local err = {
            type = 'ClientError',
            name = 'CANT_UPDATE_PRIMARY_KEY',
            message = "Attempt to modify a tuple field which is part of " ..
                      "primary index in space '_space'",
        }
        t.assert_error_covers(err, _space.update, _space,
                              {_space.id}, {{'-', 1, 1}})
        t.assert_error_covers(err, _space.update, _space,
                              {_space.id}, {{'-', 1, 2}})

        -- Incorrect sql in space opts.
        t.assert_error_covers({
            type = 'ClientError',
            name = 'WRONG_SPACE_OPTIONS',
            message = "Wrong space options: 'sql' must be string",
        }, _space.replace, _space,
           {600, ADMIN, 'test', 'memtx', 0, {sql = 100}, {}})

        local s = box.schema.create_space('test')
        s:create_index('pk')

        t.assert_error_covers({
            type = 'IllegalParams',
            message = "unexpected option 'unknown'",
        }, s.alter, s, {unknown = true})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'Use space:alter(...) instead of space.alter(...)',
        }, s.alter, {})
    end)
end

g.test_alter_deleted_space = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        s:drop()

        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_SPACE',
            message = "Space 'test' does not exist",
        }, s.alter, s, {})
        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_SPACE',
        }, box.schema.space.alter, s.id, {})
    end)
end

g.test_alter_field_count = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')

        -- Invalid values.
        local err = {
            type = 'IllegalParams',
            message = "options parameter 'field_count' should be of type " ..
                      'number',
        }
        t.assert_error_covers(err, s.alter, s, {field_count = box.NULL})
        t.assert_error_covers(err, s.alter, s, {field_count = 'string'})

        -- Can't update on non-empty space.
        s:replace({1})
        err = {
            type = 'ClientError',
            name = 'EXACT_FIELD_COUNT',
            message = 'Tuple field count 1 does not match space field ' ..
                      'count 2',
        }
        t.assert_error_covers(err, s.alter, s, {field_count = 2})
        s:delete({1})

        -- Can update on empty space.
        s:alter({field_count = 2})
        t.assert_error_covers(err, s.replace, s, {1})
        s:replace({1, 1})

        -- When not specified or nil - ignored.
        s:alter({field_count = nil})
        t.assert_error_covers(err, s.replace, s, {2})
        s:replace({2, 2})

        -- Set to 0 drops the restriction.
        s:alter({field_count = 0})
        s:truncate()
        s:replace({1})
        s:delete({1})

        -- exact_field_count is less than index_field_count.
        s:alter({field_count = 1})
        s:create_index('sk', {parts = {2, 'unsigned'}})
        t.assert_error_covers({
            type = 'ClientError',
            name = 'FIELD_MISSING',
            message = 'Tuple field 2 required by space format is missing',
        }, s.replace, s, {1})
    end)
end

g.after_test('test_alter_user', function(cg)
    cg.server:exec(function()
        box.schema.user.drop('test', {if_exists = true})
    end)
end)

g.test_alter_user = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        local _space = box.space._space
        local _user = box.space._user

        -- Invalid values.
        local err = {
            type = 'IllegalParams',
            message = "options parameter 'user' should be one of types: " ..
                      'string, number',
        }
        t.assert_error_covers(err, s.alter, s, {user = box.NULL})
        t.assert_error_covers(err, s.alter, s, {user = true})
        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_USER',
            message = "User 'not_existing' is not found",
        }, s.alter, s, {user = 'not_existing'})

        local guest_id = box.session.uid()
        box.schema.user.create('test')
        local test_id = _user.index.name:get('test').id

        -- When not specified or nil - ignored.
        s:alter({user = nil})
        t.assert_equals(_space:get({s.id}).owner, guest_id)

        s:alter({user = 'test'})
        t.assert_equals(_space:get({s.id}).owner, test_id)

        s:alter({user = guest_id})
        t.assert_equals(_space:get({s.id}).owner, guest_id)
    end)
end

g.test_alter_format = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')

        -- Invalid values.
        local err = {
            type = 'IllegalParams',
            message = "options parameter 'format' should be of type table",
        }
        t.assert_error_covers(err, s.alter, s, {format = box.NULL})
        t.assert_error_covers(err, s.alter, s, {format = true})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'format[1]: name (string) is expected',
        }, s.alter, s, {format = {{{1, 2, 3, 4}}}})

        local format = {{name = 'field1', type = 'unsigned'}}
        s:replace({1})
        s:alter({format = format})
        t.assert_equals(s:format(), format)

        -- When not specified or nil - ignored.
        s:alter({format = nil})
        t.assert_equals(s:format(), format)

        -- The format is applied to the old tuples too.
        local tuple = s:replace({1})
        t.assert_equals(tuple.field1, 1)
        s:alter({format = {}})
        t.assert_equals(tuple.field1, nil)
        s:delete({1})

    end)
end

g.test_alter_temporary = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')

        -- Invalid values.
        local err = {
            type = 'IllegalParams',
            message = "options parameter 'temporary' should be of type " ..
                      'boolean',
        }
        t.assert_error_covers(err, s.alter, s, {temporary = box.NULL})
        t.assert_error_covers(err, s.alter, s, {temporary = 100})

        s:alter({temporary = true})
        t.assert_equals(s.temporary, true)

        -- When not specified or nil - ignored.
        s:alter({temporary = nil})
        t.assert_equals(s.temporary, true)

        s:alter({temporary = false})
        t.assert_equals(s.temporary, false)

        -- Ensure absence is not treated like 'true'.
        s:alter({temporary = nil})
        t.assert_equals(s.temporary, false)
    end)
end

g.before_test('test_alter_is_sync', function(cg)
    cg.save_cfg = cg.server:exec(function()
        local save_cfg = {
            replication_synchro_quorum = box.cfg.replication_synchro_quorum,
            replication_synchro_timeout = box.cfg.replication_synchro_timeout,
        }
        box.cfg{
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 0.001,
        }
        return save_cfg
    end)
end)

g.after_test('test_alter_is_sync', function(cg)
    cg.server:exec(function(save_cfg)
        box.cfg(save_cfg)
    end, {cg.save_cfg})
    cg.save_cfg = nil
end)

g.test_alter_is_sync = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')

        -- Invalid values.
        local err = {
            type = 'IllegalParams',
            message = "options parameter 'is_sync' should be of type " ..
                      'boolean',
        }
        t.assert_error_covers(err, s.alter, s, {is_sync = box.NULL})
        t.assert_error_covers(err, s.alter, s, {is_sync = 100})

        s:alter({is_sync = true})
        t.assert_equals(s.is_sync, true)
        t.assert_error_covers({
            type = 'ClientError',
            name = 'SYNC_QUEUE_UNCLAIMED',
            message = "The synchronous transaction queue doesn't belong " ..
                      'to any instance',
        }, s.replace, s, {1})

        -- When not specified or nil - ignored.
        s:alter({is_sync = nil})
        t.assert_equals(s.is_sync, true)

        s:alter({is_sync = false})
        t.assert_equals(s.is_sync, false)

        -- Ensure absence is not treated like 'true'.
        s:alter({is_sync = nil})
        t.assert_equals(s.is_sync, false)

        s:replace({1})
        s:delete({1})
    end)
end

g.test_alter_name = function(cg)
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')

        -- Invalid values.
        local err = {
            type = 'IllegalParams',
            message = "options parameter 'name' should be of type string",
        }
        t.assert_error_covers(err, s.alter, s, {name = box.NULL})
        t.assert_error_covers(err, s.alter, s, {name = 100})

        s:alter({name = 'test2'})
        t.assert_not_equals(box.space.test2, nil)
        t.assert_equals(box.space.test, nil)
        t.assert_equals(s.name, 'test2')

        -- When not specified or nil - ignored.
        s:alter({name = nil})
        t.assert_not_equals(box.space.test2, nil)
        t.assert_equals(box.space.test, nil)
        t.assert_equals(s.name, 'test2')

        t.assert_error_covers({
            type = 'ClientError',
            name = 'TUPLE_FOUND',
        }, s.alter, s, {name = '_space'})

        s:alter({name = 'test'})
        t.assert_not_equals(box.space.test, nil)
        t.assert_equals(box.space.test2, nil)
        t.assert_equals(s.name, 'test')
    end)
end
