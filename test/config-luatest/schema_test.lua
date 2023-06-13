local ffi = require('ffi')
local fun = require('fun')
local schema = require('internal.config.utils.schema')
local t = require('luatest')

local g = t.group()

ffi.cdef([[
    struct mycdata {
        int x;
    };
]])

-- {{{ Schema node constructors: scalar, record, map, array

-- schema.scalar() must return a table of the following shape.
--
-- {
--     type = <...>,
--     <..annnotations..>
-- }
g.test_scalar_constructor = function()
    -- Several simple good cases.
    local types = {
        'string',
        'number',
        'integer',
        'boolean',
        'any',
    }
    for _, scalar_type in ipairs(types) do
        local def = {type = scalar_type}
        t.assert_equals(schema.scalar(def), def)
        local def = {type = scalar_type, my_annotation = 'info'}
        t.assert_equals(schema.scalar(def), def)
    end

    -- Several simple bad cases.
    --
    -- Ignore error messages. They are just 'assertion failed at
    -- line X', purely for the schema creator.
    local bad_scalar_defs = {
        5,
        '',
        {},
        {type = 'unknown'},
        {foo = 'bar'},
        {1, 2, 3},
    }
    for _, def in ipairs(bad_scalar_defs) do
        t.assert_equals((pcall(schema.scalar, def)), false)
    end
end

-- schema.record() must return a table of the following shape.
--
-- {
--     type = 'record',
--     fields = <...>,
--     <..annotations..>
-- }
g.test_record_constructor = function()
    local scalar_1 = schema.scalar({type = 'string'})
    local scalar_2 = schema.scalar({type = 'number'})

    -- A simple good case.
    t.assert_equals(schema.record({
        foo = scalar_1,
        bar = scalar_2,
    }), {
        type = 'record',
        fields = {
            foo = scalar_1,
            bar = scalar_2,
        },
    })

    -- A simple good case with annotations.
    t.assert_equals(schema.record({
        foo = scalar_1,
        bar = scalar_2,
    }, {
        my_annotation = 'info',
    }), {
        type = 'record',
        fields = {
            foo = scalar_1,
            bar = scalar_2,
        },
        my_annotation = 'info',
    })

    -- An empty record is allowed.
    --
    -- No args -- an empty record is created.
    t.assert_equals(schema.record({}), {type = 'record', fields = {}})
    t.assert_equals(schema.record(), {type = 'record', fields = {}})

    -- Same named field and annotation are allowed.
    t.assert_equals(schema.record({
        foo = scalar_1,
    }, {
        foo = 'info',
    }), {
        type = 'record',
        fields = {
            foo = scalar_1,
        },
        foo = 'info',
    })

    -- Bad cases: 'type' or 'fields' annotations.
    --
    -- Ignore error messages. They are just 'assertion failed at
    -- line X', purely for the schema creator.
    local fields = {foo = scalar_1}
    t.assert_equals((pcall(schema.record, fields, {type = 'info'})), false)
    t.assert_equals((pcall(schema.record, fields, {fields = 'info'})), false)
end

-- schema.map() must return a table of the following shape.
--
-- {
--     type = 'map',
--     key = <...>,
--     value = <...>,
--     <..annotations..>
-- }
g.test_map_constructor = function()
    local scalar_1 = schema.scalar({type = 'string'})
    local scalar_2 = schema.scalar({type = 'number'})

    -- A simple good case.
    t.assert_equals(schema.map({
        key = scalar_1,
        value = scalar_2,
    }), {
        type = 'map',
        key = scalar_1,
        value = scalar_2,
    })

    -- A simple good case with annotations.
    t.assert_equals(schema.map({
        key = scalar_1,
        value = scalar_2,
        my_annotation = 'info',
    }), {
        type = 'map',
        key = scalar_1,
        value = scalar_2,
        my_annotation = 'info',
    })

    -- Simple bad cases.
    --
    -- No args, no key, no value.
    --
    -- Ignore error messages. They are just 'assertion failed at
    -- line X', purely for the schema creator.
    t.assert_equals((pcall(schema.map)), false)
    t.assert_equals((pcall(schema.map, {})), false)
    t.assert_equals((pcall(schema.map, {value = scalar_1})), false)
    t.assert_equals((pcall(schema.map, {key = scalar_1})), false)

    -- Bad case: 'type' annotation is forbidden.
    --
    -- Even if it is 'map'.
    local def = {key = scalar_1, value = scalar_2, type = 'info'}
    t.assert_equals((pcall(schema.map, def)), false)
    local def = {key = scalar_1, value = scalar_2, type = 'map'}
    t.assert_equals((pcall(schema.map, def)), false)
end

-- schema.array() must return a table of the following shape.
--
-- {
--     type = 'array',
--     items = <...>,
--     <..annotations..>
-- }
g.test_array_constructor = function()
    local scalar = schema.scalar({type = 'string'})

    -- A simple good case.
    t.assert_equals(schema.array({
        items = scalar,
    }), {
        type = 'array',
        items = scalar,
    })

    -- A simple good case with annotations.
    t.assert_equals(schema.array({
        items = scalar,
        my_annotation = 'info',
    }), {
        type = 'array',
        items = scalar,
        my_annotation = 'info',
    })

    -- Simple bad cases.
    --
    -- No args, no items.
    --
    -- Ignore error messages. They are just 'assertion failed at
    -- line X', purely for the schema creator.
    t.assert_equals((pcall(schema.array)), false)
    t.assert_equals((pcall(schema.array, {})), false)

    -- Bad case: 'type' annotation is forbidden.
    --
    -- Even if it is 'array'.
    local def = {items = scalar, type = 'info'}
    t.assert_equals((pcall(schema.array, def)), false)
    local def = {items = scalar, type = 'array'}
    t.assert_equals((pcall(schema.array, def)), false)
end

-- }}} Schema node constructors: scalar, record, map, array

-- {{{ Schema object constructor: new

-- schema.new() must return a table of the following shape.
--
-- {
--     name = <string>,
--     schema = <...>,
--     methods = <...>
-- }
--
-- It should have a metatable with methods provided by the
-- schema module and methods provided in the constructor
-- arguments.
g.test_schema_new = function()
    local scalar_1 = schema.scalar({type = 'string'})
    local scalar_2 = schema.scalar({type = 'number'})

    -- Several simple good cases.
    local schema_nodes = {
        scalar_1,
        schema.record({foo = scalar_1}),
        schema.map({key = scalar_1, value = scalar_2}),
        schema.array({items = scalar_1}),
    }
    for _, schema_node in ipairs(schema_nodes) do
        local name = 'myschema'
        local s = schema.new(name, schema_node)
        t.assert_equals(s.name, name)
        t.assert_equals(s.schema, schema_node)
        t.assert_equals(s.methods, {})
        t.assert(getmetatable(s) ~= nil)

        local mymethods = {
            foo = function(self)
                return {'foo', self}
            end
        }
        local s = schema.new(name, schema_node, {methods = mymethods})
        t.assert_equals(s.methods.foo, mymethods.foo)
        t.assert_equals(s:foo(), {'foo', s})
    end

    -- Bad cases: no args, no name, no schema node, wrong name
    -- type, wrong schema node type.
    --
    -- Ignore error messages. They are just 'assertion failed at
    -- line X', purely for the schema creator.
    t.assert_equals((pcall(schema.new)), false)
    t.assert_equals((pcall(schema.new, 'myschema')), false)
    t.assert_equals((pcall(schema.new, 'myschema', 5)), false)
    t.assert_equals((pcall(schema.new, 'myschema', 'foo')), false)
    t.assert_equals((pcall(schema.new, nil, scalar_1)), false)
    t.assert_equals((pcall(schema.new, 5, scalar_1)), false)
    t.assert_equals((pcall(schema.new, {}, scalar_1)), false)
end

-- }}} Schema object constructor: new

-- {{{ Testing helpers for <schema object>:validate()

local samples = {
    nil,
    'foo',
    5,
    true,
    {},
    function() end,
    newproxy(),
    box.NULL,
    1LL,
    1ULL,
    ffi.new('struct mycdata', {x = 5}),
}

-- Verify that :validate() raises the given error on the given
-- data.
local function assert_validate_error(s, data, exp_err_msg)
    t.assert_error_msg_equals(exp_err_msg, function()
        s:validate(data)
    end)
end

-- Verify that :validate() on a scalar schema raises the given
-- error prefixed with a context information on the given data.
local function assert_validate_scalar_error(s, data, exp_err_msg)
    local prefix = ('[%s] Unexpected data for scalar "%s": '):format(s.name,
        s.schema.type)
    assert_validate_error(s, data, prefix .. exp_err_msg)
end

-- Verify that :validate() on a scalar schema raises the error
-- regarding a type mismatch on the given data.
local function assert_validate_scalar_type_mismatch(s, data, exp_type)
    local exp_err_msg = ('Expected "%s", got "%s"'):format(exp_type, type(data))
    assert_validate_scalar_error(s, data, exp_err_msg)
end

-- Verify that :validate() on a scalar schema raises the error
-- regarding a type mismatch on all data samples except ones of
-- the given type.
local function assert_validate_scalar_expects_only_given_type(s, exp_type)
    for i = 1, table.maxn(samples) do
        local data = samples[i]
        if type(data) ~= exp_type then
            assert_validate_scalar_type_mismatch(s, data, exp_type)
        end
    end
end

-- Verify that :validate() on a record/a map/an array schema
-- raises the error regarding a type mismatch on the given data.
local function assert_validate_compound_type_mismatch(s, data)
    local schema_str = s.schema.type == 'record' and 'a record' or
        s.schema.type == 'map' and 'a map' or
        s.schema.type == 'array' and 'an array'
    assert(type(schema_str) == 'string')
    local exp_err_msg = ('[%s] Unexpected data type for %s: "%s"'):format(
        s.name, schema_str, type(data))
    assert_validate_error(s, data, exp_err_msg)
end

-- Verify that :validate() on a record/a map/an array schema
-- raises the error regarding a type mismatch on all data samples
-- except ones of the table type.
local function assert_validate_compound_type_expects_only_table(s)
    for i = 1, table.maxn(samples) do
        local data = samples[i]
        if type(data) ~= 'table' then
            assert_validate_compound_type_mismatch(s, data)
        end
    end
end

-- }}} Testing helpers for <schema object>:validate()

-- {{{ <schema object>:validate()

g.test_validate_string = function()
    local s = schema.new('myschema', schema.scalar({type = 'string'}))

    -- Good cases.
    s:validate('')
    s:validate('foo')

    -- Bad cases.
    assert_validate_scalar_expects_only_given_type(s, 'string')
end

g.test_validate_number = function()
    local s = schema.new('myschema', schema.scalar({type = 'number'}))

    -- Good cases.
    s:validate(-5.3)
    s:validate(-5)
    s:validate(0)
    s:validate(5)
    s:validate(5.3)

    -- Bad cases.
    assert_validate_scalar_expects_only_given_type(s, 'number')

    -- TODO: +inf, -inf, NaN.
end

g.test_validate_integer = function()
    local s = schema.new('myschema', schema.scalar({type = 'integer'}))

    -- Good cases.
    s:validate(-5)
    s:validate(0)
    s:validate(5)

    -- Bad cases.
    assert_validate_scalar_expects_only_given_type(s, 'number')
    assert_validate_scalar_error(s, 5.5,
        'Expected number without a fractional part, got 5.5')

    -- TODO: +inf, -inf, NaN.
end

g.test_validate_boolean = function()
    local s = schema.new('myschema', schema.scalar({type = 'boolean'}))

    -- Good cases.
    s:validate(false)
    s:validate(true)

    -- Bad cases.
    assert_validate_scalar_expects_only_given_type(s, 'boolean')
end

g.test_validate_any = function()
    local s = schema.new('myschema', schema.scalar({type = 'any'}))

    -- Accepts anything.
    for i = 1, table.maxn(samples) do
        local data = samples[i]
        s:validate(data)
    end

    -- TODO: +inf, -inf, NaN.
end

g.test_validate_record = function()
    local scalar = schema.scalar({type = 'string'})
    local s = schema.new('myschema', schema.record({foo = scalar}))

    -- Good cases.
    --
    -- All the fields are taken as optional, so an empty table is
    -- OK.
    s:validate({foo = 'bar'})
    s:validate({})

    -- Bad cases: data of a wrong type is passed as a record.
    assert_validate_compound_type_expects_only_table(s)

    -- Bad case: unknown field.
    assert_validate_error(s, {bar = true}, '[myschema] Unexpected field "bar"')

    -- Bad case: data of a wrong type is passed to a field of the
    -- record.
    assert_validate_error(s, {foo = true}, table.concat({
        '[myschema] foo: Unexpected data for scalar "string"',
        'Expected "string", got "boolean"',
    }, ': '))
end

g.test_validate_map = function()
    local scalar_1 = schema.scalar({type = 'string'})
    local scalar_2 = schema.scalar({type = 'number'})
    local s = schema.new('myschema', schema.map({
        key = scalar_1,
        value = scalar_2,
    }))

    -- Good cases.
    s:validate({})
    s:validate({foo = 5})
    s:validate({foo = 5, bar = 6})

    -- Bad case: data of a wrong type is passed as a map.
    assert_validate_compound_type_expects_only_table(s)

    -- Bad case: data of a wrong type is passed to a key.
    assert_validate_error(s, {[1] = true}, table.concat({
        '[myschema] [1]: Unexpected data for scalar "string"',
        'Expected "string", got "number"',
    }, ': '))

    -- Bad case: data of a wrong type is passed to a field value.
    assert_validate_error(s, {foo = true}, table.concat({
        '[myschema] foo: Unexpected data for scalar "number"',
        'Expected "number", got "boolean"',
    }, ': '))
end

g.test_validate_array = function()
    local scalar = schema.scalar({type = 'string'})
    local s = schema.new('myschema', schema.array({
        items = scalar,
    }))

    -- Good cases.
    s:validate({})
    s:validate({'foo'})
    s:validate({'foo', 'bar'})
    s:validate({'foo', 'bar', 'baz'})

    -- Bad cases: data of a wrong type is passed as a record.
    assert_validate_compound_type_expects_only_table(s)

    -- Bad case: non-numeric key.
    assert_validate_error(s, {foo = 'bar'},
        '[myschema] An array contains a non-numeric key: "foo"')

    -- Bad case: floating-point key.
    assert_validate_error(s, {[1] = 'foo', [1.5] = 'bar', [3] = 'baz'},
        '[myschema] An array contains a non-integral numeric key: 1.5')

    -- Bad case: minimal index is not 1.
    assert_validate_error(s, {[2] = 'foo', [3] = 'bar'},
        '[myschema] An array must start from index 1, got min index 2')

    -- Bad case: an array with holes.
    assert_validate_error(s, {[1] = 'foo', [3] = 'bar'},
        '[myschema] An array must not have holes, got a table with 2 ' ..
        'integer fields with max index 3')

    -- Bad case: data of a wrong type is passed to an items.
    assert_validate_error(s, {true}, table.concat({
        '[myschema] [1]: Unexpected data for scalar "string"',
        'Expected "string", got "boolean"',
    }, ': '))
end

g.test_validate_by_allowed_values = function()
    local allowed_values = {
        0, 'fatal',
        1, 'syserror',
        2, 'error',
        3, 'crit',
        4, 'warn',
        5, 'info',
        6, 'verbose',
        7, 'debug',
    }

    local s = schema.new('log_level', schema.scalar({
        type = 'any',
        allowed_values = allowed_values,
    }))

    -- Good cases: all the allowed values are actually allowed.
    for _, level in ipairs(allowed_values) do
        s:validate(level)
    end

    -- Bad cases: other values are not allowed.
    assert_validate_error(s, -1, ('[log_level] Got %s, but only the ' ..
        'following values are allowed: %s'):format(-1,
        table.concat(allowed_values, ', ')))

    -- Verify that a type validation occurs before the allowed
    -- values validation.
    local s = schema.new('fruits', schema.scalar({
        type = 'string',
        allowed_values = {'orange', 'banana'},
    }))
    assert_validate_scalar_type_mismatch(s, 1, 'string')
end

g.test_validate_by_node_function = function()
    local schema_saved
    local path_saved
    local error_saved

    local scalar = schema.scalar({
        type = 'integer',
        validate = function(data, w)
            schema_saved = w.schema
            path_saved = w.path
            error_saved = w.error
            if data < 0 then
                w.error('The value should be positive, got %d', data)
            end
        end,
    })

    local s = schema.new('positive', schema.record({
        foo = schema.record({
            bar = scalar,
        }),
    }))

    -- Good case.
    s:validate({foo = {bar = 1}})

    -- Verify that the schema node is the one that contains the
    -- `validate` annotation.
    t.assert_equals(schema_saved, scalar)

    -- Verify that the path is not changed during the traversal
    -- and still points to the given schema node.
    t.assert_equals(path_saved, {'foo', 'bar'})

    -- Verify that the error function works just like before even
    -- after the traversal. The main point here is that the
    -- foo.bar path is present in the message.
    local exp_err_msg = '[positive] foo.bar: call after the traversal'
    t.assert_error_msg_equals(exp_err_msg, function()
        error_saved('call after the %s', 'traversal')
    end)

    -- Bad case.
    assert_validate_error(s, {foo = {bar = -1}},
        '[positive] foo.bar: The value should be positive, got -1')

    -- Verify that a type validation occurs before the schema node
    -- function call.
    assert_validate_error(s, {foo = {bar = false}}, table.concat({
        '[positive] foo.bar: Unexpected data for scalar "integer"',
        'Expected "number", got "boolean"',
    }, ': '))
end

-- }}} <schema object>:validate()

-- {{{ <schema object>:get()

-- A schema with the record on the top level.
g.test_get_no_path_record = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'string'}),
        }),
    }))

    -- nil, '' and {} mean the root node.
    local data = {foo = {bar = 'baz'}}
    t.assert_equals(s:get(data), data)
    t.assert_equals(s:get(data, ''), data)
    t.assert_equals(s:get(data, {}), data)
end

-- Same for a map.
g.test_get_no_path_map = function()
    local s = schema.new('myschema', schema.map({
        key = schema.scalar({type = 'string'}),
        value = schema.scalar({type = 'string'}),
    }))

    -- nil, '' and {} mean the root node.
    local data = {foo = 'baz'}
    t.assert_equals(s:get(data), data)
    t.assert_equals(s:get(data, ''), data)
    t.assert_equals(s:get(data, {}), data)
end

-- Same for an array.
g.test_get_no_path_array = function()
    local s = schema.new('myschema', schema.array({
        items = schema.scalar({type = 'string'}),
    }))

    -- nil, '' and {} mean the root node.
    local data = {'foo', 'bar', 'baz'}
    t.assert_equals(s:get(data), data)
    t.assert_equals(s:get(data, ''), data)
    t.assert_equals(s:get(data, {}), data)
end

-- Scalar on the top level.
--
-- This is a kind of a corner case and, it seems, there is no
-- much sense in calling :get() on a scalar schema.
--
-- OTOH, there is no reason to forbid this case and reduce the
-- applicability of the method.
g.test_get_no_path_scalar = function()
    local s = schema.new('myschema', schema.scalar({type = 'string'}))

    local data = 'mydata'
    t.assert_equals(s:get(data), data)
    t.assert_equals(s:get(data, ''), data)
    t.assert_equals(s:get(data, {}), data)
end

-- Quite same, but for a scalar of the any type.
g.test_get_no_path_scalar_any = function()
    local s = schema.new('myschema', schema.scalar({type = 'any'}))

    local data = {foo = {bar = 'baz'}}
    t.assert_equals(s:get(data), data)
    t.assert_equals(s:get(data, ''), data)
    t.assert_equals(s:get(data, {}), data)
end

-- Index a record.
g.test_get_nested_in_record = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'string'}),
        }),
    }))

    -- Existing data.
    local data = {foo = {bar = 'baz'}}
    t.assert_equals(s:get(data, 'foo'), {bar = 'baz'})
    t.assert_equals(s:get(data, 'foo.bar'), 'baz')

    -- Non-existing data. Verify that :get() works in the optional
    -- chaining way.
    local data = {}
    t.assert_equals(s:get(data, 'foo'), nil)
    t.assert_equals(s:get(data, 'foo.bar'), nil)
end

-- Verify that there are no problems with composite data in a
-- scalar of the any type.
g.test_get_nested_any_in_record = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'any'}),
        }),
    }))

    local scalar_data = {baz = {fiz = 'mydata'}}
    local data = {foo = {bar = scalar_data}}
    t.assert_equals(s:get(data, 'foo'), {bar = scalar_data})
    t.assert_equals(s:get(data, 'foo.bar'), scalar_data)
end

-- Index a map.
g.test_get_nested_in_map = function()
    local s = schema.new('myschema', schema.map({
        key = schema.scalar({type = 'string'}),
        value = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'string'}),
        }),
    }))

    -- Existing data.
    local data = {foo = {bar = 'baz'}}
    t.assert_equals(s:get(data, 'foo'), {bar = 'baz'})
    t.assert_equals(s:get(data, 'foo.bar'), 'baz')

    -- The same using array-like table paths.
    local data = {foo = {bar = 'baz'}}
    t.assert_equals(s:get(data, {'foo'}), {bar = 'baz'})
    t.assert_equals(s:get(data, {'foo', 'bar'}), 'baz')

    -- Non-existing data. Verify that :get() works in the optional
    -- chaining way.
    local data = {}
    t.assert_equals(s:get(data, 'foo'), nil)
    t.assert_equals(s:get(data, 'foo.bar'), nil)

    -- The same using array-like table paths.
    local data = {}
    t.assert_equals(s:get(data, {'foo'}), nil)
    t.assert_equals(s:get(data, {'foo', 'bar'}), nil)
end

-- Trigger improper API usage errors.
g.test_get_usage = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'string'}),
        }),
    }))

    local exp_err_msg = 'Usage: schema:get(data: <as defined by the ' ..
        'schema>, path: nil/string/table)'

    -- A top level record `data` can't be nil.
    --
    -- This case looks like a mistake, so the usage error is
    -- raised.
    t.assert_error_msg_equals(exp_err_msg, function()
        s:get()
    end)

    -- A top level record `data` can't be a string.
    --
    -- This case looks like the `data` argument is forgotten, so
    -- the usage error is raised.
    t.assert_error_msg_equals(exp_err_msg, function()
        s:get('foo')
    end)

    -- A top level record `data` can't be a string.
    --
    -- This case looks like the `data` and the `path` arguments
    -- are misordered, so the usage error is raised.
    t.assert_error_msg_equals(exp_err_msg, function()
        s:get('foo.bar', {foo = {bar = 'baz'}})
    end)

    -- The `path` is not a table and not a string.
    t.assert_error_msg_equals(exp_err_msg, function()
        s:get({foo = {bar = 'baz'}}, 5)
    end)
end

-- Attempt to get an unknown record's field.
--
-- The path is verified against the schema. It is impossible to
-- get a field that doesn't exist in the record.
g.test_get_unknown_record_field = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'string'}),
        }),
    }))

    local exp_err_msg = '[myschema] unknown: No such field in the schema'
    t.assert_error_msg_equals(exp_err_msg, function()
        s:get({}, 'unknown')
    end)

    local exp_err_msg = '[myschema] foo.unknown: No such field in the schema'
    t.assert_error_msg_equals(exp_err_msg, function()
        s:get({}, 'foo.unknown')
    end)
end

-- Attempt to index an array.
g.test_get_index_array = function()
    local s = schema.new('myschema', schema.array({
        items = schema.scalar({type = 'string'}),
    }))

    local exp_err_msg = '[myschema] Indexing an array is not supported yet'
    t.assert_error_msg_equals(exp_err_msg, function()
        s:get({}, '[1]')
    end)
end

-- Attempt to index a scalar value.
g.test_get_index_scalar = function()
    -- Indexing a scalar is forbidden.
    local s = schema.new('myschema', schema.scalar({type = 'string'}))
    local exp_err_msg = '[myschema] Attempt to index a scalar value of type ' ..
        'string by field "foo"'
    t.assert_error_msg_equals(exp_err_msg, function()
        local data = 5
        s:get(data, 'foo')
    end)

    -- The scalar of the 'any' type is the same in this regard.
    local s = schema.new('myschema', schema.scalar({type = 'any'}))
    local exp_err_msg = '[myschema] Attempt to index a scalar value of type ' ..
        'any by field "foo"'
    t.assert_error_msg_equals(exp_err_msg, function()
        local data = {foo = {bar = 'baz'}}
        s:get(data, 'foo')
    end)
end

-- }}} <schema object>:get()

-- {{{ <schema object>:set()

-- Set a record's field.
g.test_set_record_field = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'string'}),
        }),
        goo = schema.record({
            bar = schema.scalar({type = 'string'}),
            baz = schema.scalar({type = 'string'}),
        }),
    }))

    -- <data>.foo = <...>
    local data = {}
    s:set(data, 'foo', {bar = 'mydata'})
    t.assert_equals(data, {foo = {bar = 'mydata'}})

    -- Subsequent :set() calls are cumulative: existing data is
    -- kept, the only changed field is one pointed by `path`.
    s:set(data, 'goo', {bar = 'mydata'})
    t.assert_equals(data, {
        foo = {bar = 'mydata'},
        goo = {bar = 'mydata'},
    })
    s:set(data, 'goo.baz', 'mydata')
    t.assert_equals(data, {
        foo = {bar = 'mydata'},
        goo = {bar = 'mydata', baz = 'mydata'},
    })

    -- The same using array-like table paths.
    local data = {}
    s:set(data, {'foo'}, {bar = 'mydata'})
    t.assert_equals(data, {foo = {bar = 'mydata'}})

    -- <data>.foo.bar = <...>
    local data = {}
    s:set(data, 'foo.bar', 'mydata')
    t.assert_equals(data, {foo = {bar = 'mydata'}})

    -- The same using array-like table paths.
    local data = {}
    s:set(data, {'foo', 'bar'}, 'mydata')
    t.assert_equals(data, {foo = {bar = 'mydata'}})
end

-- Verify that there are no problems with composite data in a
-- scalar of the any type.
g.test_set_record_field_any = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'any'}),
        }),
    }))

    -- <data>.foo = <...>
    local data = {}
    local any = {baz = {fiz = 'mydata'}}
    s:set(data, 'foo', {bar = any})
    t.assert_equals(data, {foo = {bar = any}})

    -- <data>.foo.bar = <...>
    local data = {}
    local any = {baz = {fiz = 'mydata'}}
    s:set(data, 'foo.bar', any)
    t.assert_equals(data, {foo = {bar = any}})
end

-- Set a map's field.
g.test_set_map_field = function()
    local s = schema.new('myschema', schema.map({
        key = schema.scalar({type = 'string'}),
        value = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'string'}),
        }),
    }))

    -- <data>.foo = <...>
    local data = {}
    s:set(data, 'foo', {bar = 'mydata'})
    t.assert_equals(data, {foo = {bar = 'mydata'}})

    -- Subsequent :set() calls are cumulative: existing data is
    -- kept, the only changed field is one pointed by `path`.
    s:set(data, 'goo', {bar = 'mydata'})
    t.assert_equals(data, {
        foo = {bar = 'mydata'},
        goo = {bar = 'mydata'},
    })
    s:set(data, 'goo.baz', 'mydata')
    t.assert_equals(data, {
        foo = {bar = 'mydata'},
        goo = {bar = 'mydata', baz = 'mydata'},
    })

    -- The same using array-like table paths.
    local data = {}
    s:set(data, {'foo'}, {bar = 'mydata'})
    t.assert_equals(data, {foo = {bar = 'mydata'}})

    -- <data>.foo.bar = <...>
    local data = {}
    s:set(data, 'foo.bar', 'mydata')
    t.assert_equals(data, {foo = {bar = 'mydata'}})

    -- The same using array-like table paths.
    local data = {}
    s:set(data, {'foo', 'bar'}, 'mydata')
    t.assert_equals(data, {foo = {bar = 'mydata'}})
end

-- Trigger improper API usage error.
g.test_set_usage = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'string'}),
        }),
    }))

    local exp_err_msg = 'Usage: schema:set(data: <as defined by the ' ..
        'schema>, path: string/table, rhs: <as defined by the schema>)'

    -- A top level record `data` can't be nil.
    --
    -- This case looks like a mistake, so the usage error is
    -- raised.
    t.assert_error_msg_equals(exp_err_msg, function()
        s:set()
    end)

    -- A top level record `data` can't be a string.
    --
    -- This case looks like the `data` argument is forgotten, so
    -- the usage error is raised.
    t.assert_error_msg_equals(exp_err_msg, function()
        s:set('foo')
    end)

    -- A top level record `data` can't be a string.
    --
    -- This case looks like the `data` and the `path` arguments
    -- are misordered, so the usage error is raised.
    --
    -- Verify it with and without the `rhs` argument.
    t.assert_error_msg_equals(exp_err_msg, function()
        local data = {}
        s:set('foo.bar', data)
    end)
    t.assert_error_msg_equals(exp_err_msg, function()
        local data = {}
        s:set('foo.bar', data, 'mydata')
    end)

    -- The `path` is not a table and not a string.
    t.assert_error_msg_equals(exp_err_msg, function()
        local data = {}
        s:set(data, 5, 'mydata')
    end)
end

-- :set() can't be used with nil or empty `path`.
g.test_set_empty_path = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'string'}),
        }),
    }))

    local exp_err_msg = 'schema:set: empty path'

    t.assert_error_msg_equals(exp_err_msg, function()
        local data = {}
        s:set(data)
    end)

    t.assert_error_msg_equals(exp_err_msg, function()
        local data = {}
        s:set(data, '')
    end)

    t.assert_error_msg_equals(exp_err_msg, function()
        local data = {}
        s:set(data, {})
    end)
end

-- Attempt to set an unknown record's field.
--
-- The path is verified against the schema. It is impossible to
-- set a field that doesn't exist in the record.
g.test_set_unknown_field = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'string'}),
        }),
    }))

    local exp_err_msg = '[myschema] unknown: No such field in the schema'
    local data = {}
    t.assert_error_msg_equals(exp_err_msg, function()
        s:set(data, 'unknown', 'mydata')
    end)

    -- Verify that the data remains unchanged after an error.
    t.assert_equals(data, {})

    local exp_err_msg = '[myschema] foo.unknown: No such field in the schema'
    local data = {}
    t.assert_error_msg_equals(exp_err_msg, function()
        s:set(data, 'foo.unknown', 'mydata')
    end)

    -- Verify that the data remains unchanged after an error.
    t.assert_equals(data, {})
end

-- Attempt to set an item in an array.
g.test_set_index_array = function()
    local s = schema.new('myschema', schema.array({
        items = schema.scalar({type = 'string'}),
    }))

    local exp_err_msg = '[myschema] Indexing an array is not supported yet'
    local data = {}
    t.assert_error_msg_equals(exp_err_msg, function()
        s:set(data, '[1]', 'mydata')
    end)

    -- Verify that the data remains unchanged after an error.
    t.assert_equals(data, {})
end

-- Attempt to index a scalar.
g.test_set_index_scalar = function()
    -- Indexing a scalar is forbidden.
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'string'}),
        }),
    }))
    local exp_err_msg = '[myschema] foo.bar: Attempt to index a scalar ' ..
        'value of type string by field "baz"'
    local data = {}
    t.assert_error_msg_equals(exp_err_msg, function()
        s:set(data, 'foo.bar.baz', 'mydata')
    end)

    -- Verify that the data remains unchanged after an error.
    t.assert_equals(data, {})

    -- The scalar of the 'any' type is the same in this regard.
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'any'}),
        }),
    }))
    local exp_err_msg = '[myschema] foo.bar: Attempt to index a scalar ' ..
        'value of type any by field "baz"'
    local data = {}
    t.assert_error_msg_equals(exp_err_msg, function()
        local rhs = {fiz = 'giz'}
        s:set(data, 'foo.bar.baz', rhs)
    end)

    -- Verify that the data remains unchanged after an error.
    t.assert_equals(data, {})
end

-- Attempt to set a value that doesn't correspond to the given
-- schema.
g.test_set_invalid_rhs = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'string'}),
        }),
    }))

    local exp_err_msg = table.concat({
        '[myschema] foo.bar',
        'Unexpected data for scalar "string"',
        'Expected "string", got "number"',
    }, ': ')
    local data = {}
    t.assert_error_msg_equals(exp_err_msg, function()
        s:set(data, 'foo.bar', 5)
    end)

    -- Verify that the data remains unchanged after an error.
    t.assert_equals(data, {})
end

-- }}} <schema object>:set()

-- {{{ Testing helpers for <schema object>:filter()

-- Run the <schema object>:filter() function and verify its return
-- value.
local function assert_filter_result(s, data, f, exp_res)
    local res = s:filter(data, f):totable()
    t.assert_equals(res, exp_res)
end

-- Run the <schema object>:filter() function and verify its
-- visited and returned list of nodes.
local function assert_filter_visited_nodes_and_result(s, data, f,
        exp_visited_nodes)
    local exp_res = fun.iter(exp_visited_nodes):filter(f):totable()

    local visited_nodes = {}
    local res = s:filter(data, function(w)
        table.insert(visited_nodes, w)
        return f(w)
    end):totable()

    t.assert_equals(visited_nodes, exp_visited_nodes)
    t.assert_equals(res, exp_res)
end

-- Generate a list of expected walkthrough nodes.
local function gen_walkthrough_node_list(gen_walkthrough_node, data, paths)
    return fun.iter(paths):map(function(path)
        return gen_walkthrough_node(data, path)
    end):totable()
end

-- }}} Testing helpers for <schema object>:filter()

-- {{{ <schema object>:filter()

-- Verify :filter() method against a schema with records.
--
-- Actually a set of test cases with a common schema and test
-- oracle generators.
g.test_filter_record = function()
    -- Put something unique into each schema node to ensure that
    -- the :filter() function passes correct nodes into its
    -- callback.
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({
                type = 'string',
                description = 'this is scalar bar',
            }),
        }, {
            description = 'this is inner record foo',
        }),
        goo = schema.record({
            car = schema.scalar({
                type = 'integer',
                description = 'this is scalar car',
            }),
        }, {
            description = 'this is inner record goo',
        }),
    }, {
        description = 'this is the outmost record',
    }))

    -- Generate expected walkthrough node by the given data and path.
    --
    -- This simple generator assumes that all the nodes in the
    -- path are record's fields.
    local function gen_walkthrough_node(data, path)
        local path = path == '' and {} or string.split(path, '.')
        return {
            path = path,
            schema = fun.iter(path):foldl(function(schema, field_name)
                return schema.fields[field_name]
            end, s.schema),
            data = fun.iter(path):foldl(function(data, field_name)
                return data[field_name]
            end, data),
        }
    end

    -- Run the 'accept all' filter function on different data to
    -- verify that it is called on all the expected schema nodes
    -- with expected argument.
    local cases = {
        -- All the fields are present and are not box.NULL.
        {
            data = {foo = {bar = 'baz'}, goo = {car = 'caz'}},
            exp_visited_paths = {'', 'foo', 'foo.bar', 'goo', 'goo.car'},
        },
        -- The data is nil.
        {
            data = nil,
            exp_visited_paths = {''},
        },
        -- The data is box.NULL.
        {
            data = box.NULL,
            exp_visited_paths = {''},
        },
        -- One field is box.NULL, another is missed.
        {
            data = {foo = box.NULL},
            exp_visited_paths = {'', 'foo'},
        },
        -- The same, but on the deeper level.
        {
            data = {foo = {bar = box.NULL}, goo = {}},
            exp_visited_paths = {'', 'foo', 'foo.bar', 'goo'},
        },
    }

    for _, case in ipairs(cases) do
        local exp_visited_nodes = gen_walkthrough_node_list(
            gen_walkthrough_node, case.data, case.exp_visited_paths)
        assert_filter_visited_nodes_and_result(s, case.data, function(_w)
            return true
        end, exp_visited_nodes)
    end

    -- Run different filter functions to verify that :filter()
    -- actually follows the return value of the filter function.
    local cases = {
        {
            func = function(_w)
                return true
            end,
            exp_res_paths = {'', 'foo', 'foo.bar', 'goo', 'goo.car'},
        }, {
            func = function(_w)
                return false
            end,
            exp_res_paths = {},
        }, {
            func = function(w)
                return w.path[1] == 'foo'
            end,
            exp_res_paths = {'foo', 'foo.bar'},
        }, {
            func = function(w)
                return w.schema.description ~= nil and
                    w.schema.description:find('inner') ~= nil
            end,
            exp_res_paths = {'foo', 'goo'},
        }, {
            func = function(w)
                return w.schema.type == 'string' or w.schema.type == 'integer'
            end,
            exp_res_paths = {'foo.bar', 'goo.car'},
        }
    }

    local data = {
        foo = {bar = 'baz'},
        goo = {car = 'caz'},
    }

    for _, case in ipairs(cases) do
        local exp_res = gen_walkthrough_node_list(
            gen_walkthrough_node, data, case.exp_res_paths)
        assert_filter_result(s, data, case.func, exp_res)
    end
end

-- Verify :filter() method against a schema with maps.
--
-- Actually a set of test cases with the common schema and
-- test oracle generators.
--
-- The test cases are adopted from the record's test case, but
-- the schema, expected node generation function, expected nodes
-- are different.
g.test_filter_map = function()
    local s = schema.new('myschema', schema.map({
        key = schema.scalar({
            type = 'string',
            description = 'a key in the outer map',
        }),
        value = schema.map({
            key = schema.scalar({
                type = 'string',
                description = 'a key in the inner map',
            }),
            value = schema.scalar({
                type = 'string',
                description = 'a value in the inner map',
            }),
            description = 'the inner map',
        }),
        description = 'the outmost map',
    }))

    -- Generate expected walkthrough node by the given data and path.
    --
    -- This simple generator assumes that all the nodes in the
    -- path are map's fields.
    local function gen_walkthrough_node(data, path)
        local path = path == '' and {} or string.split(path, '.')
        return {
            path = fun.iter(path):map(function(field_name)
                if field_name:startswith('key-') then
                    return field_name:sub(5)
                else
                    return field_name
                end
            end):totable(),
            schema = fun.iter(path):foldl(function(schema, field_name)
                if field_name:startswith('key-') then
                    return schema.key
                else
                    return schema.value
                end
            end, s.schema),
            data = fun.iter(path):foldl(function(data, field_name)
                if field_name:startswith('key-') then
                    return field_name:sub(5)
                else
                    return data[field_name]
                end
            end, data),
        }
    end

    -- Run the 'accept all' filter function on different data to
    -- verify that it is called on all the expected schema nodes
    -- with expected argument.
    local cases = {
        -- All the fields are present and are not box.NULL.
        {
            data = {foo = {bar = 'baz'}, goo = {car = 'caz'}},
            exp_visited_paths = {'', 'key-foo', 'foo', 'foo.key-bar', 'foo.bar',
                'key-goo', 'goo', 'goo.key-car', 'goo.car'},
        },
        -- The data is nil.
        {
            data = nil,
            exp_visited_paths = {''},
        },
        -- The data is box.NULL.
        {
            data = box.NULL,
            exp_visited_paths = {''},
        },
        -- One field is box.NULL, another is missed.
        {
            data = {foo = box.NULL},
            exp_visited_paths = {'', 'key-foo', 'foo'},
        },
        -- The same, but on the deeper level.
        {
            data = {foo = {bar = box.NULL}, goo = {}},
            exp_visited_paths = {'', 'key-foo', 'foo', 'foo.key-bar', 'foo.bar',
                'key-goo', 'goo'},
        },
    }

    for _, case in ipairs(cases) do
        local exp_visited_nodes = gen_walkthrough_node_list(
            gen_walkthrough_node, case.data, case.exp_visited_paths)
        assert_filter_visited_nodes_and_result(s, case.data, function(_w)
            return true
        end, exp_visited_nodes)
    end

    -- Run different filter functions to verify that :filter()
    -- actually follows the return value of the filter function.
    local cases = {
        {
            func = function(_w)
                return true
            end,
            exp_res_paths = {'', 'key-foo', 'foo', 'foo.key-bar', 'foo.bar',
                'key-goo', 'goo', 'goo.key-car', 'goo.car'},
        }, {
            func = function(_w)
                return false
            end,
            exp_res_paths = {},
        }, {
            func = function(w)
                return w.path[1] == 'foo'
            end,
            exp_res_paths = {'key-foo', 'foo', 'foo.key-bar', 'foo.bar'},
        }, {
            func = function(w)
                return w.schema.description ~= nil and
                    w.schema.description:find('inner') ~= nil
            end,
            exp_res_paths = {'foo', 'foo.key-bar', 'foo.bar', 'goo',
                'goo.key-car', 'goo.car'},
        }, {
            func = function(w)
                return w.schema.type == 'string'
            end,
            exp_res_paths = {'key-foo', 'foo.key-bar', 'foo.bar', 'key-goo',
                'goo.key-car', 'goo.car'},
        }
    }

    local data = {
        foo = {bar = 'baz'},
        goo = {car = 'caz'},
    }

    for _, case in ipairs(cases) do
        local exp_res = gen_walkthrough_node_list(
            gen_walkthrough_node, data, case.exp_res_paths)
        assert_filter_result(s, data, case.func, exp_res)
    end
end

-- Verify :filter() method against a schema with an array.
--
-- Actually a set of test cases with a common schema and test
-- oracle generators.
--
-- Follows record and map test cases by the structure, but
-- simplified.
g.test_filter_array = function()
    local s = schema.new('myschema', schema.array({
        items = schema.scalar({
            type = 'string',
        }),
    }))

    -- Generate expected walkthrough node by the given data and path.
    --
    -- This simple generator assumes that all the nodes in the
    -- path are array's items.
    local function gen_walkthrough_node(data, path)
        if path == '' then
            return {
                path = {},
                schema = s.schema,
                data = data,
            }
        elseif type(path) == 'number' then
            return {
                path = {path},
                schema = s.schema.items,
                data = data[path],
            }
        else
            assert(false)
        end
    end

    -- Run the 'accept all' filter function on different data to
    -- verify that it is called on all the expected schema nodes
    -- with expected argument.
    local cases = {
        -- Usual array.
        {
            data = {'foo', 'bar'},
            exp_visited_paths = {'', 1, 2},
        },
        -- The data is nil.
        {
            data = nil,
            exp_visited_paths = {''},
        },
        -- The data is box.NULL.
        {
            data = box.NULL,
            exp_visited_paths = {''},
        },
        -- One item is box.NULL.
        {
            data = {'foo', box.NULL},
            exp_visited_paths = {'', 1, 2},
        }
    }

    for _, case in ipairs(cases) do
        local exp_visited_nodes = gen_walkthrough_node_list(
            gen_walkthrough_node, case.data, case.exp_visited_paths)
        assert_filter_visited_nodes_and_result(s, case.data, function(_w)
            return true
        end, exp_visited_nodes)
    end

    -- Run different filter functions to verify that :filter()
    -- actually follows the return value of the filter function.
    local cases = {
        {
            func = function(_w)
                return true
            end,
            exp_res_paths = {'', 1, 2},
        }, {
            func = function(_w)
                return false
            end,
            exp_res_paths = {},
        }, {
            func = function(w)
                return next(w.path) ~= nil
            end,
            exp_res_paths = {1, 2},
        }, {
            func = function(w)
                return w.path[1] == 2
            end,
            exp_res_paths = {2},
        }, {
            func = function(w)
                return w.schema.type == 'string'
            end,
            exp_res_paths = {1, 2},
        }, {
            func = function(w)
                return w.data == 'foo'
            end,
            exp_res_paths = {1},
        }
    }

    local data = {'foo', 'bar'}

    for _, case in ipairs(cases) do
        local exp_res = gen_walkthrough_node_list(
            gen_walkthrough_node, data, case.exp_res_paths)
        assert_filter_result(s, data, case.func, exp_res)
    end
end

-- }}} <schema object>:filter()
