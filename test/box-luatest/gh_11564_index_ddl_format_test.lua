local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
        cg.server = nil
    end
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

-- Create space 'test' with a primary index; the 'value' field is
-- declared with the given is_nullable value, or an options table
-- ({is_nullable, nullable_action, type}) for less common cases.
local function create_test_space(cg, opts)
    if type(opts) ~= 'table' then
        opts = {is_nullable = opts}
    end
    cg.server:exec(function(opts)
        local space = box.schema.space.create('test')
        space:format({
            {name = 'id', type = 'unsigned'},
            {name = 'value', type = opts.type or 'unsigned',
             is_nullable = opts.is_nullable,
             nullable_action = opts.nullable_action,
             scale = opts.scale},
        })
        space:create_index('pk')
    end, {opts})
end

-- Create a secondary index on the 'value' field with the given
-- is_nullable value, or an options table ({is_nullable, type}) for
-- less common cases.
local function create_index(cg, name, opts)
    if type(opts) ~= 'table' then
        opts = {is_nullable = opts}
    end
    cg.server:exec(function(name, opts)
        box.space.test:create_index(name, {
            parts = {{field = 'value', is_nullable = opts.is_nullable,
                      type = opts.type, scale = opts.scale}},
        })
    end, {name, opts})
end

local function alter_index(cg, name, is_nullable)
    cg.server:exec(function(name, is_nullable)
        box.space.test.index[name]:alter({
            parts = {{field = 'value', is_nullable = is_nullable}},
        })
    end, {name, is_nullable})
end

local function drop_index(cg, name)
    cg.server:exec(function(name)
        box.space.test.index[name]:drop()
    end, {name})
end

-- Assert that both public format views (space:format() and
-- space.format_object) report the given is_nullable for a field.
local function assert_field_nullable(cg, field_no, expected)
    cg.server:exec(function(field_no, expected)
        local space = box.space.test
        t.assert_equals(space:format()[field_no].is_nullable, expected)
        t.assert_equals(
            space.format_object:totable()[field_no].is_nullable, expected)
    end, {field_no, expected})
end

-- Assert that a field's is_nullable option is absent (not exported)
-- in both public format views.
local function assert_field_nullable_absent(cg, field_no)
    cg.server:exec(function(field_no)
        local space = box.space.test
        t.assert_equals(space:format()[field_no].is_nullable, nil)
        t.assert_equals(
            space.format_object:totable()[field_no].is_nullable, nil)
    end, {field_no})
end

-- Assert that both public format views report the given type for a
-- field.
local function assert_field_type(cg, field_no, expected)
    cg.server:exec(function(field_no, expected)
        local space = box.space.test
        t.assert_equals(space:format()[field_no].type, expected)
        t.assert_equals(
            space.format_object:totable()[field_no].type, expected)
    end, {field_no, expected})
end

local function assert_field_scale(cg, field_no, expected)
    cg.server:exec(function(field_no, expected)
        local space = box.space.test
        t.assert_equals(space:format()[field_no].scale, expected)
        t.assert_equals(
            space.format_object:totable()[field_no].scale, expected)
    end, {field_no, expected})
end

-- Assert that replacing a tuple with NULL in the nullable field
-- succeeds, consistently with the reported nullable format.
local function assert_null_replace_ok(cg)
    cg.server:exec(function()
        local space = box.space.test
        space:replace{1, box.NULL}
        space:delete{1}
    end)
end

-- Assert that replacing/validating a tuple with NULL fails,
-- consistently with the reported non-nullable format.
local function assert_null_replace_fails(cg)
    cg.server:exec(function()
        local space = box.space.test
        local expected = {
            type = 'ClientError',
            code = box.error.FIELD_TYPE,
            message = 'Tuple field 2 (value) type does not match one ' ..
                      'required by operation: expected unsigned, got nil',
        }
        t.assert_error_covers(expected, space.replace, space, {1, box.NULL})
        t.assert_error_covers(expected, space.format_object.validate,
                               space.format_object, {1, box.NULL})
    end)
end

-- Create a non-nullable secondary index on a nullable field: the
-- reported format must become non-nullable, and NULL must be rejected.
g.test_index_create_makes_nullable_visible_false = function(cg)
    create_test_space(cg, true)
    assert_field_nullable(cg, 2, true)

    create_index(cg, 'value', false)

    assert_field_nullable(cg, 2, false)
    assert_null_replace_fails(cg)
end

-- Alter a non-nullable index to nullable: the reported format must
-- become nullable again, and NULL must be accepted.
g.test_index_alter_to_nullable_makes_visible_true = function(cg)
    create_test_space(cg, true)
    create_index(cg, 'value', false)
    assert_field_nullable(cg, 2, false)

    alter_index(cg, 'value', true)

    assert_field_nullable(cg, 2, true)
    assert_null_replace_ok(cg)
end

-- Drop a non-nullable index that was tightening a nullable field: the
-- reported format must be restored to nullable.
g.test_index_drop_restores_nullable = function(cg)
    create_test_space(cg, true)
    create_index(cg, 'value', false)
    assert_field_nullable(cg, 2, false)

    drop_index(cg, 'value')

    assert_field_nullable(cg, 2, true)
    assert_null_replace_ok(cg)
end

-- Two non-nullable indexes on the same nullable field: dropping one
-- must not restore nullability; dropping the last one must.
g.test_multiple_non_nullable_indexes = function(cg)
    create_test_space(cg, true)
    create_index(cg, 'sk1', false)
    create_index(cg, 'sk2', false)
    assert_field_nullable(cg, 2, false)

    drop_index(cg, 'sk1')
    assert_field_nullable(cg, 2, false)

    drop_index(cg, 'sk2')
    assert_field_nullable(cg, 2, true)
end

-- A field without an explicit is_nullable in the original format must
-- stay absent in both views, even after a non-nullable index.
g.test_omitted_is_nullable_stays_absent = function(cg)
    create_test_space(cg, nil)

    create_index(cg, 'value', false)

    assert_field_nullable_absent(cg, 1)
    assert_field_nullable_absent(cg, 2)
end

-- is_nullable and nullable_action must stay consistent with each
-- other after an index narrows the effective nullable action.
g.test_nullable_action_stays_consistent_with_is_nullable = function(cg)
    create_test_space(cg, {is_nullable = true, nullable_action = 'none'})
    create_index(cg, 'sk', false)

    cg.server:exec(function()
        local space = box.space.test
        local exported = space:format()
        t.assert_equals(exported[2].is_nullable, false)
        t.assert_equals(exported[2].nullable_action, 'default')
        space:format(exported)
    end)
end

-- An index can narrow a field's reported type too, not just its
-- nullability, and the type must revert once the index is dropped.
g.test_index_narrows_reported_type = function(cg)
    create_test_space(cg, {type = 'any'})
    assert_field_type(cg, 2, 'any')

    create_index(cg, 'sk', {type = 'unsigned'})
    assert_field_type(cg, 2, 'unsigned')

    cg.server:exec(function()
        t.assert_error_covers(
            {type = 'ClientError', code = box.error.FIELD_TYPE},
            box.space.test.replace, box.space.test, {1, 'string'})
    end)

    drop_index(cg, 'sk')
    assert_field_type(cg, 2, 'any')
end

g.test_index_narrows_reported_type_params = function(cg)
    create_test_space(cg, {type = 'any'})

    create_index(cg, 'sk', {type = 'decimal32', scale = 2})
    assert_field_type(cg, 2, 'decimal32')
    assert_field_scale(cg, 2, 2)

    cg.server:exec(function()
        local space = box.space.test
        space:format(space:format())
    end)
end

g.test_names_only_tuple_format_serialize = function()
    local format = box.internal.tuple_format.new({
        {name = 'a', type = 'unsigned'},
    }, true)
    t.assert_equals(format:totable(), {
        {name = 'a', type = 'unsigned'},
    })
end
