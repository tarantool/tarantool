local ffi = require('ffi')
local fun = require('fun')
local schema = require('experimental.config.utils.schema')
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

        -- TODO: Remove when a union node will be implemented.
        'string, number',
        'number, string',
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

-- {{{ Derived schema node type constructors: enum, set

-- schema.enum() must return a table of the following shape.
--
-- {
--     type = 'string',
--     allowed_values = <...>,
--     <..annnotations..>
-- }
g.test_enum_constructor = function()
    -- A simple good case.
    t.assert_equals(schema.enum({'foo', 'bar'}), {
        type = 'string',
        allowed_values = {'foo', 'bar'},
    })

    -- A good case with an annotation.
    t.assert_equals(schema.enum({'foo', 'bar'}, {
        my_annotation = 'info',
    }), {
        type = 'string',
        allowed_values = {'foo', 'bar'},
        my_annotation = 'info',
    })

    -- Simple bad cases.
    --
    -- 'type' or 'allowed_values' annotation.
    --
    -- Ignore error messages. They are just 'assertion failed at
    -- line X', purely for the schema creator.
    local def = {'foo', 'bar'}
    t.assert_equals(pcall(schema.enum, def, {type = 'number'}), false)
    t.assert_equals(pcall(schema.enum, def, {allowed_values = {'foo'}}), false)
end

-- schema.set() must return a table of the following shape.
--
-- {
--     type = 'array',
--     items = schema.scalar({
--         type = 'string',
--         allowed_values = <...>,
--     }),
--     unique_items = true,
--     <..annotations..>
-- }
g.test_set_constructor = function()
    -- A simple good case.
    t.assert_equals(schema.set({'foo', 'bar'}), {
        type = 'array',
        items = {
            type = 'string',
            allowed_values = {'foo', 'bar'},
        },
        unique_items = true,
    })

    -- A simple good case with an annotation.
    t.assert_equals(schema.set({'foo', 'bar'}, {
        my_annotation = 'info',
    }), {
        type = 'array',
        items = {
            type = 'string',
            allowed_values = {'foo', 'bar'},
        },
        unique_items = true,
        my_annotation = 'info',
    })

    -- Simple bad cases.
    --
    -- 'type', 'items' or 'validate' annotation.
    --
    -- Ignore error messages. They are just 'assertion failed at
    -- line X', purely for the schema creator.
    local scalar = schema.scalar({type = 'string'})
    local def = {'foo', 'bar'}
    t.assert_equals(pcall(schema.set, def, {type = 'number'}), false)
    t.assert_equals(pcall(schema.set, def, {items = scalar}), false)
end

-- }}} Derived schema node type constructors: enum, set

-- {{{ Testing helpers for comparing schema nodes

local function remove_computed_fields(schema)
    local res = table.copy(schema)
    res.computed = nil

    if schema.type == 'record' then
        local fields = {}
        for k, v in pairs(res.fields) do
            fields[k] = remove_computed_fields(v)
        end
        res.fields = fields
    elseif schema.type == 'array' then
        res.items = remove_computed_fields(res.items)
    elseif schema.type == 'map' then
        res.key = remove_computed_fields(res.key)
        res.value = remove_computed_fields(res.value)
    end

    return res
end

-- }}} Testing helpers for comparing schema nodes

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
        t.assert_equals(remove_computed_fields(s.schema), schema_node)
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

-- Verify that if a schema node from one schema is used in another
-- schema, computed fields from the former schema don't affect
-- computed fields from the latter one.
g.test_schema_node_reuse = function()
    local s1 = schema.new('s1', schema.record({
        foo = schema.scalar({
            type = 'string',
            my_annotation_2 = 2,
        }),
    }, {
        my_annotation_1 = 1,
    }))

    local s2 = schema.new('s2', schema.record({
        bar = s1.schema.fields.foo,
    }, {
        my_annotation_3 = 3,
    }))

    t.assert_equals(s2.schema.fields.bar.computed.annotations, {
        -- Key property: there is no my_annotation_1 here.
        my_annotation_2 = 2,
        my_annotation_3 = 3,
    })
end

-- Verify computed annotations in schema nodes.
g.test_computed_annotations = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.map({
            key = schema.scalar({
                type = 'string',
                level_3 = 3,
                clash = 'c',
            }),
            value = schema.array({
                items = schema.scalar({
                    type = 'integer',
                    level_4 = 4,
                    clash = 'd',
                }),
                level_3 = 3,
                clash = 'c',
            }),
            level_2 = 2,
            clash = 'b',
        }),
    }, {
        level_1 = 1,
        clash = 'a',
    }))

    t.assert_equals(s.schema, schema.record({
        foo = schema.map({
            key = schema.scalar({
                type = 'string',
                level_3 = 3,
                clash = 'c',
                computed = {
                    annotations = {
                        level_1 = 1,
                        level_2 = 2,
                        level_3 = 3,
                        clash = 'c',
                    },
                },
            }),
            value = schema.array({
                items = schema.scalar({
                    type = 'integer',
                    level_4 = 4,
                    clash = 'd',
                    computed = {
                        annotations = {
                            level_1 = 1,
                            level_2 = 2,
                            level_3 = 3,
                            level_4 = 4,
                            clash = 'd',
                        },
                    },
                }),
                level_3 = 3,
                clash = 'c',
                computed = {
                    annotations = {
                        level_1 = 1,
                        level_2 = 2,
                        level_3 = 3,
                        clash = 'c',
                    },
                },
            }),
            level_2 = 2,
            clash = 'b',
            computed = {
                annotations = {
                    level_1 = 1,
                    level_2 = 2,
                    clash = 'b',
                },
            },
        }),
    }, {
        level_1 = 1,
        clash = 'a',
        computed = {
            annotations = {
                level_1 = 1,
                clash = 'a',
            },
        },
    }))
end

-- Verify that the following fields are ignored when collecting
-- computed annotations.
--
-- * allowed_values
-- * unique_items
-- * validate
-- * default
-- * apply_default_if
g.test_computed_annotations_ignore = function()
    local s = schema.new('myschema', schema.scalar({
        type = 'string',
        allowed_values = {'x'},
        validate = function() end,
        default = 'x',
        apply_default_if = function() return true end,
        my_annotation = 'my annotation',
    }))

    t.assert_equals(s.schema.computed, {
        annotations = {
            my_annotation = 'my annotation',
        },
    })

    -- NB: unique_items is allowed only in an array.
    local s = schema.new('myschema', schema.array({
        items = schema.scalar({type = 'string'}),
        unique_items = true,
        my_annotation = 'my annotation',
    }))

    t.assert_equals(s.schema.computed, {
        annotations = {
            my_annotation = 'my annotation',
        },
    })
end

-- Verify that the unique_items annotation is only allowed in an
-- array of strings.
g.test_validate_schema = function()
    -- Not an array.
    local exp_err_msg = '[myschema] foo.bar: "unique_items" requires an ' ..
        'array of strings, got a string'
    t.assert_error_msg_equals(exp_err_msg, function()
        schema.new('myschema', schema.record({
            foo = schema.record({
                bar = schema.scalar({
                    type = 'string',
                    unique_items = true,
                }),
            }),
        }))
    end)

    -- Array of non-string values.
    local exp_err_msg = '[myschema] foo.bar: "unique_items" requires an ' ..
        'array of strings, got an array of integer items'
    t.assert_error_msg_equals(exp_err_msg, function()
        schema.new('myschema', schema.record({
            foo = schema.record({
                bar = schema.array({
                    items = schema.scalar({
                        type = 'integer',
                    }),
                    unique_items = true,
                }),
            }),
        }))
    end)
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

-- TODO: Remove when a union node will be implemented.
g.test_validate_string_number = function()
    local s1 = schema.new('myschema', schema.scalar({type = 'string, number'}))
    local s2 = schema.new('myschema', schema.scalar({type = 'number, string'}))

    -- Good cases.
    for _, s in ipairs({s1, s2}) do
        s:validate('')
        s:validate('foo')
        s:validate(-5.3)
        s:validate(-5)
        s:validate(0)
        s:validate(5)
        s:validate(5.3)
    end

    -- Bad cases.
    for _, s in ipairs({s1, s2}) do
        local exp_err_fmt = 'Expected one of "string", "number", got %q'
        for i = 1, table.maxn(samples) do
            local data = samples[i]
            if type(data) ~= 'string' and type(data) ~= 'number' then
                local exp_err_msg = exp_err_fmt:format(type(data))
                assert_validate_scalar_error(s, data, exp_err_msg)
            end
        end
    end
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

-- Validate an array data against a record schema.
g.test_validate_record_with_array_data = function()
    -- Good case: an array is passed for a record with numeric
    -- keys that has key 1.
    local s = schema.new('myschema', schema.record({
        [1] = schema.scalar({type = 'string'}),
        [2] = schema.scalar({type = 'string'}),
        [3] = schema.scalar({type = 'string'}),
    }))
    s:validate({'a', 'b', 'c'})

    -- Bad case: numeric keys, but no key 1.
    local s = schema.new('myschema', schema.record({
        [2] = schema.scalar({type = 'string'}),
        [3] = schema.scalar({type = 'string'}),
    }))
    assert_validate_error(s, {'a', 'b'},
        '[myschema] Expected a record, got an array: ["a","b"]')

    -- Bad case: string keys.
    local s = schema.new('myschema', schema.record({
        foo = schema.scalar({type = 'string'}),
        bar = schema.scalar({type = 'string'}),
    }))
    assert_validate_error(s, {'a', 'b', 'c'},
        '[myschema] Expected a record, got an array: ["a","b","c"]')
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
    --
    -- Note: {[1] = true} would trigger an array check, which is
    -- verified in test_validate_map_with_array_data().
    assert_validate_error(s, {[2] = true}, table.concat({
        '[myschema] [2]: Unexpected data for scalar "string"',
        'Expected "string", got "number"',
    }, ': '))

    -- Bad case: data of a wrong type is passed to a field value.
    assert_validate_error(s, {foo = true}, table.concat({
        '[myschema] foo: Unexpected data for scalar "number"',
        'Expected "number", got "boolean"',
    }, ': '))
end

-- Validate an array data against a map schema.
g.test_validate_map_with_array_data = function()
    -- Good cases: an array is passed for a map that accepts
    -- numeric keys.
    for _, key_type in ipairs({
        'number',
        'number, string',
        'string, number',
        'integer',
        'any'})
    do
        local s = schema.new('myschema', schema.map({
            key = schema.scalar({type = key_type}),
            value = schema.scalar({type = 'string'}),
        }))
        s:validate({'a', 'b', 'c'})
    end

    -- Define schema nodes of a composite type to use them as a
    -- map key in the scenario below.
    local record_node = schema.record({
        foo = schema.scalar({type = 'string'}),
    })
    local map_node = schema.map({
        key = schema.scalar({type = 'string'}),
        value = schema.scalar({type = 'string'}),
    })
    local array_node = schema.array({
        items = schema.scalar({type = 'string'}),
    })

    -- Bad cases: an array is passed for a map that doesn't accept
    -- numeric keys: string, boolean, record, map, array.
    for _, key_schema in ipairs({
        schema.scalar({type = 'string'}),
        schema.scalar({type = 'boolean'}),
        record_node,
        map_node,
        array_node})
    do
        local s = schema.new('myschema', schema.map({
            key = key_schema,
            value = schema.scalar({type = 'string'}),
        }))
        assert_validate_error(s, {'a', 'b', 'c'},
            '[myschema] Expected a map, got an array: ["a","b","c"]')
    end
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

g.test_validate_unique_items = function()
    local s = schema.new('myschema', schema.set({'foo', 'bar'}, {
        validate = function(_data, w)
            w.error('error from the validate annotation')
        end,
    }))

    -- The uniqueness check is performed before the user-provided
    -- validation function.
    local exp_err = '[myschema] Values should be unique, but "foo" ' ..
        'appears at least twice'
    t.assert_error_msg_equals(exp_err, s.validate, s, {'foo', 'foo'})

    -- The user-provided validation function is taken into
    -- account.
    local exp_err = '[myschema] error from the validate annotation'
    t.assert_error_msg_equals(exp_err, s.validate, s, {'foo'})
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
    t.assert_equals(remove_computed_fields(schema_saved), scalar)

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

g.test_validate_enum = function()
    local allowed_values = {
        'fatal',
        'syserror',
        'error',
        'crit',
        'warn',
        'info',
        'verbose',
        'debug',
    }

    local s = schema.new('log_level', schema.enum(allowed_values))

    -- Good cases: all the allowed values are actually allowed.
    for _, level in ipairs(allowed_values) do
        s:validate(level)
    end

    -- Bad cases: other values are not allowed.
    assert_validate_error(s, 'foo', ('[log_level] Got %s, but only the ' ..
        'following values are allowed: %s'):format('foo',
        table.concat(allowed_values, ', ')))

    -- Verify that a type validation occurs before the allowed
    -- values validation.
    assert_validate_scalar_type_mismatch(s, 1, 'string')
end

g.test_validate_set = function()
    local allowed_values = {
        'read',
        'write',
        'execute',
    }

    local s = schema.new('permissions', schema.set(allowed_values))

    -- Good cases: all the allowed values are actually allowed.
    s:validate({'read'})
    s:validate({'write'})
    s:validate({'execute'})

    -- Good cases: two different allowed values are allowed.
    s:validate({'read', 'write'})
    s:validate({'write', 'read'})
    s:validate({'read', 'execute'})
    s:validate({'execute', 'read'})
    s:validate({'write', 'execute'})
    s:validate({'execute', 'write'})

    -- Good cases: three different allowed values are allowed.
    s:validate({'read', 'write', 'execute'})
    s:validate({'write', 'read', 'execute'})
    s:validate({'read', 'execute', 'write'})
    s:validate({'execute', 'read', 'write'})
    s:validate({'write', 'execute', 'read'})
    s:validate({'execute', 'write', 'read'})

    -- Bad cases: duplicates are not allowed.
    local exp_err_fmt = '[permissions] Values should be unique, but "%s" ' ..
        'appears at least twice'
    assert_validate_error(s, {'read', 'read'}, exp_err_fmt:format('read'))
    assert_validate_error(s, {'write', 'write'}, exp_err_fmt:format('write'))
    assert_validate_error(s, {'execute', 'execute'},
        exp_err_fmt:format('execute'))
    assert_validate_error(s, {'read', 'execute', 'read'},
        exp_err_fmt:format('read'))
    assert_validate_error(s, {'execute', 'write', 'write'},
        exp_err_fmt:format('write'))
    assert_validate_error(s, {'execute', 'execute', 'execute'},
        exp_err_fmt:format('execute'))

    -- Bad cases: other values are not allowed.
    assert_validate_error(s, {'foo'}, ('[permissions] [1]: Got %s, but only ' ..
        'the following values are allowed: %s'):format('foo',
        table.concat(allowed_values, ', ')))

    -- Verify that a type validation occurs before the allowed
    -- values validation.
    assert_validate_error(s, 1, '[permissions] Unexpected data type for an ' ..
        'array: "number"')
    assert_validate_error(s, {1}, table.concat({
        '[permissions] [1]: Unexpected data for scalar "string"',
        'Expected "string", got "number"',
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

-- Verify that :get() allows to get a nested value from a scalar
-- of the any type.
g.test_get_nested_in_any = function()
    local s = schema.new('myschema', schema.scalar({type = 'any'}))

    -- Existing data.
    local data = {foo = {bar = 'baz'}}
    t.assert_equals(s:get(data, 'foo'), {bar = 'baz'})
    t.assert_equals(s:get(data, 'foo.bar'), 'baz')

    -- Non-existing data. Verify that :get() works in the optional
    -- chaining way.
    local data = {}
    t.assert_type(s:get(data, 'foo'), 'nil')
    t.assert_type(s:get(data, 'foo.bar'), 'nil')

    -- The same, but the non-existing field is box.NULL.
    --
    -- Indexing of nil/box.NULL gives nil.
    local data = {foo = box.NULL}
    t.assert_type(s:get(data, 'foo.bar'), 'nil')

    -- The same, but nil/box.NULL is on the 'any' scalar level,
    -- not inside.
    local data = nil
    t.assert_type(s:get(data, 'foo'), 'nil')
    local data = box.NULL
    t.assert_type(s:get(data, 'foo'), 'nil')

    -- If the path points to box.NULL, it is returned as is (not
    -- as nil).
    local data = {foo = box.NULL}
    t.assert_type(s:get(data, 'foo'), 'cdata')

    -- Attempt to index a primitive value (except nil/box.NULL).
    local exp_err_msg = '[myschema] foo.bar: Attempt to index a non-table ' ..
        'value (number) by field "baz"'
    t.assert_error_msg_equals(exp_err_msg, function()
        local data = {foo = {bar = 42}}
        s:get(data, 'foo.bar.baz')
    end)
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
    -- Indexing a scalar is forbidden (except 'any').
    local s = schema.new('myschema', schema.scalar({type = 'string'}))
    local exp_err_msg = '[myschema] Attempt to index a scalar value of type ' ..
        'string by field "foo"'
    t.assert_error_msg_equals(exp_err_msg, function()
        local data = 5
        s:get(data, 'foo')
    end)
end

-- This scenario has been broken before gh-10855.
-- Before gh-10855 schema:get() used to change the passed path
-- if it's been passed as a table.
g.test_get_not_changes_path = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.scalar({type = 'number'}),
    }))

    local path = {'foo'}
    -- The result of schema:get() isn't tested.
    s:get({foo = 5}, path)
    -- Make sure the passed path hasn't changed.
    t.assert_equals(path, {'foo'})
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

-- gh-10193: verify that record's field can be set to box.NULL.
g.test_set_record_field_to_null = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'string'}),
        }),
    }))

    local data = {}
    s:set(data, 'foo', box.NULL)
    t.assert_type(data.foo, 'cdata')

    local data = {}
    s:set(data, 'foo.bar', box.NULL)
    t.assert_type(data.foo, 'table')
    t.assert_type(data.foo.bar, 'cdata')
end

-- gh-10190: verify that box.NULL record is replaced by a table if
-- a nested field is set.
g.test_set_over_null_record = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'string'}),
        }),
    }))

    local data = {foo = box.NULL}
    s:set(data, 'foo.bar', 'mydata')
    t.assert_equals(data, {foo = {bar = 'mydata'}})
end

-- gh-10194: verify that a record's field can be deleted.
g.test_set_record_field_to_nil = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.record({
            bar = schema.scalar({type = 'string'}),
            baz = schema.scalar({type = 'string'}),
        }),
    }))

    -- Basic scenario (no-op).
    local data = {}
    s:set(data, 'foo', nil)
    t.assert_equals(data, {})

    -- Basic scenario (actual deletion).
    local data = {foo = {bar = 'x', baz = 'y'}}
    s:set(data, 'foo.bar', nil)
    t.assert_equals(data, {foo = {baz = 'y'}})

    -- Don't create intermediate tables.
    local data = {}
    s:set(data, 'foo.bar', nil)
    t.assert_equals(data, {})

    -- Don't drop existing tables.
    local data = {foo = {bar = 'x'}}
    s:set(data, 'foo.bar', nil)
    t.assert_equals(data, {foo = {}})

    -- box.NULL is kept if a deletion of a nested field is
    -- requested.
    local data = {foo = box.NULL}
    s:set(data, 'foo.bar', nil)
    t.assert_type(data.foo, 'cdata')
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

-- gh-10193: verify that map's field can be set to box.NULL.
g.test_set_map_field_to_null = function()
    local s = schema.new('myschema', schema.map({
        key = schema.scalar({type = 'string'}),
        value = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'string'}),
        }),
    }))

    local data = {}
    s:set(data, 'foo', box.NULL)
    t.assert_type(data.foo, 'cdata')

    local data = {}
    s:set(data, 'foo.bar', box.NULL)
    t.assert_type(data.foo, 'table')
    t.assert_type(data.foo.bar, 'cdata')
end

-- gh-10190: verify that box.NULL map is replaced by a table if
-- a nested field is set.
g.test_set_over_null_map = function()
    local s = schema.new('myschema', schema.map({
        key = schema.scalar({type = 'string'}),
        value = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'string'}),
        }),
    }))

    local data = {foo = box.NULL}
    s:set(data, 'foo.bar', 'mydata')
    t.assert_equals(data, {foo = {bar = 'mydata'}})
end

-- gh-10194: verify that a map's field can be deleted.
g.test_set_map_field_to_nil = function()
    local s = schema.new('myschema', schema.map({
        key = schema.scalar({type = 'string'}),
        value = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'string'}),
        }),
    }))

    -- Basic scenario (no-op).
    local data = {}
    s:set(data, 'foo', nil)
    t.assert_equals(data, {})

    -- Basic scenario (actual deletion).
    local data = {foo = {bar = 'x', baz = 'y'}}
    s:set(data, 'foo.bar', nil)
    t.assert_equals(data, {foo = {baz = 'y'}})

    -- Don't create intermediate tables.
    local data = {}
    s:set(data, 'foo.bar', nil)
    t.assert_equals(data, {})

    -- Don't drop existing tables.
    local data = {foo = {bar = 'x'}}
    s:set(data, 'foo.bar', nil)
    t.assert_equals(data, {foo = {}})

    -- box.NULL is kept if a deletion of a nested field is
    -- requested.
    local data = {foo = box.NULL}
    s:set(data, 'foo.bar', nil)
    t.assert_type(data.foo, 'cdata')
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

-- This scenario has been broken before gh-10855.
-- Before gh-10855 schema:set() used to change the passed path
-- if it's been passed as a table.
g.test_set_not_changes_path = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.scalar({type = 'number'}),
    }))

    local path = {'foo'}
    -- The result of schema:set() isn't tested.
    s:set({foo = 5}, path, 6)
    -- Make sure the passed path hasn't changed.
    t.assert_equals(path, {'foo'})
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

-- {{{ Testing helpers for <schema object>:map()

-- A simple function to pass into <schema object>:map().
--
-- It substitutes ${x} patterns in string values with given
-- variables.
local function apply_vars(data, w, vars)
    if w.schema.no_transform then
        return data
    end

    if w.schema.type == 'string' and data ~= nil then
        assert(type(data) ~= nil)
        return (data:gsub('%${(.-)}', function(var_name)
            if vars[var_name] ~= nil then
                return vars[var_name]
            end
            w.error(('Unknown variable %q'):format(var_name))
        end))
    end
    return data
end

-- Another function to pass into <schema object>:map().
--
-- Substitutes each scalar value with the path from the root
-- schema node.
local function value2path(data, w)
    if w.schema.no_transform then
        return data
    end

    -- Don't transform values of a composite type.
    if w.schema.type == 'record' or w.schema.type == 'map' or
       w.schema.type == 'array' then
        return data
    end

    return table.concat(w.path, '.')
end

-- Yet another function to pass into <schema object>:map().
--
-- Substitutes each scalar value with its Lua type.
local function value2type(data, w)
    if w.schema.no_transform then
        return data
    end

    -- Don't transform values of a composite type.
    if w.schema.type == 'record' or w.schema.type == 'map' or
       w.schema.type == 'array' then
        return data
    end

    return type(data)
end

-- Compare types deeply.
--
-- The main motivation is to differentiate nil and box.NULL.
--
-- The following calls do not raise an error:
--
-- * t.assert_equals(nil, box.NULL)
-- * t.assert_equals(box.NULL, nil) too.
-- * t.assert_equals({foo = box.NULL}, {foo = nil})
--
-- However, the following does:
--
-- * t.assert_equals({foo = nil}, {foo = box.NULL})
--
-- See also https://github.com/tarantool/luatest/issues/310
local function assert_type_equals(a, b)
    t.assert_equals(type(a), type(b))
    if type(a) ~= 'table' then
        return
    end
    assert(type(b) == 'table')

    for k, v in pairs(a) do
        assert_type_equals(v, b[k])
    end
    for k, v in pairs(b) do
        assert_type_equals(v, a[k])
    end
end

-- }}} Testing helpers for <schema object>:map()

-- {{{ <schema object>:map()

-- Verify :map() method on a schema with a record.
g.test_map_record = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.scalar({type = 'string'}),
        bar = schema.scalar({type = 'string'}),
        baz = schema.scalar({type = 'number'}),
    }))

    -- Several simple successful cases.
    --
    -- The apply_vars function verifies most of the logic.
    --
    -- value2path verifies that w.path is correct and that
    -- the record is always fully traversed (including
    -- nil/box.NULL fields and when the record itself is
    -- nil/box.NULL).
    --
    -- value2type verifies that the original nil/cdata type
    -- is preserved for nil/box.NULL values.
    local cases = {
        -- All the fields are exist.
        {
            data = {foo = 'a${b}c', bar = 'd${e}f', baz = 5},
            vars = {b = 'B', e = 'E'},
            exp_apply_vars = {foo = 'aBc', bar = 'dEf', baz = 5},
            exp_value2path = {foo = 'foo', bar = 'bar', baz = 'baz'},
            exp_value2type = {foo = 'string', bar = 'string', baz = 'number'},
        },
        -- One field is box.NULL, others are missed.
        {
            data = {foo = box.NULL},
            vars = {},
            exp_apply_vars = {foo = box.NULL},
            exp_value2path = {foo = 'foo', bar = 'bar', baz = 'baz'},
            exp_value2type = {foo = 'cdata', bar = 'nil', baz = 'nil'},
        },
        -- The record is an empty table.
        {
            data = {},
            vars = {},
            exp_apply_vars = {},
            exp_value2path = {foo = 'foo', bar = 'bar', baz = 'baz'},
            exp_value2type = {foo = 'nil', bar = 'nil', baz = 'nil'},
        },
        -- The record is box.NULL.
        {
            data = box.NULL,
            vars = {},
            exp_apply_vars = box.NULL,
            exp_value2path = {foo = 'foo', bar = 'bar', baz = 'baz'},
            exp_value2type = {foo = 'nil', bar = 'nil', baz = 'nil'},
        },
        -- The record is nil.
        {
            data = nil,
            vars = {},
            exp_apply_vars = nil,
            exp_value2path = {foo = 'foo', bar = 'bar', baz = 'baz'},
            exp_value2type = {foo = 'nil', bar = 'nil', baz = 'nil'},
        },
    }

    for _, case in ipairs(cases) do
        local res = s:map(case.data, apply_vars, case.vars)
        t.assert_equals(res, case.exp_apply_vars)
        assert_type_equals(res, case.exp_apply_vars)

        local res = s:map(case.data, value2path)
        t.assert_equals(res, case.exp_value2path)
        assert_type_equals(res, case.exp_value2path)

        local res = s:map(case.data, value2type)
        t.assert_equals(res, case.exp_value2type)
        assert_type_equals(res, case.exp_value2type)
    end

    -- Verify that an error that is raised using w.error() has the
    -- context information: the schema name and the path.
    local exp_err_msg = '[myschema] foo: Unknown variable "x"'
    t.assert_error_msg_equals(exp_err_msg, function()
        local data = {foo = '${x}'}
        local vars = {} -- no variable 'x'
        s:map(data, apply_vars, vars)
    end)
end

-- Verify that the :map() method allows to transform a record
-- value.
--
-- This functionality was implemented in gh-10756.
g.test_map_record_itself = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.scalar({type = 'string'}),
        bar = schema.scalar({type = 'string'}),
        baz = schema.scalar({type = 'string'}),
        packed = schema.scalar({type = 'string'}),
    }))

    -- This scenario performs the following transformations.
    --
    -- First, a record with one `packed` field is transformed to
    -- a record with three fields: `foo`, `bar`, `baz`.
    --
    -- These fields contain ${a}, ${b}, and ${c} placeholders,
    -- which are replaced with the provided variable values.
    --
    -- This way we verify that the traversal is pre-order.
    local vars = {
        a = 'foo',
        b = 'bar',
        c = 'baz',
    }
    local res = s:map({
        packed = '${a},${b},${c}',
    }, function(data, w, vars)
        -- Unpack a packed record.
        if w.schema.type == 'record' and data.packed ~= nil then
            assert(type(data.packed) == 'string')
            local items = data.packed:split(',')
            return {
                foo = items[1],
                bar = items[2],
                baz = items[3],
            }
        end
        -- Apply variables to string values.
        return apply_vars(data, w, vars)
    end, vars)

    t.assert_equals(res, {
        foo = 'foo',
        bar = 'bar',
        baz = 'baz',
    })
end

-- Verify :map() method on a schema with a map.
g.test_map_map = function()
    local s = schema.new('myschema', schema.map({
        key = schema.scalar({type = 'string', no_transform = true}),
        value = schema.map({
            key = schema.scalar({type = 'string', no_transform = true}),
            value = schema.scalar({type = 'string'}),
        }),
    }))

    -- apply_vars verifies most of the logic.
    --
    -- value2path shows that w.path is correct.
    --
    -- value2type here is just for completeness.
    local cases = {
        -- The innermost value is present.
        {
            data = {foo = {bar = 'a${b}c'}},
            vars = {b = 'B'},
            exp_apply_vars = {foo = {bar = 'aBc'}},
            exp_value2path = {foo = {bar = 'foo.bar'}},
            exp_value2type = {foo = {bar = 'string'}},
        },
        -- The innermost value is box.NULL.
        {
            data = {foo = {bar = box.NULL}},
            vars = {},
            exp_apply_vars = {foo = {bar = box.NULL}},
            exp_value2path = {foo = {bar = 'foo.bar'}},
            exp_value2type = {foo = {bar = 'cdata'}},
        },
        -- The innermost value is nil.
        {
            data = {foo = {}},
            vars = {},
            exp_apply_vars = {foo = {}},
            exp_value2path = {foo = {}},
            exp_value2type = {foo = {}},
        },
        -- The outermost map field is box.NULL.
        {
            data = {foo = box.NULL},
            vars = {},
            exp_apply_vars = {foo = box.NULL},
            exp_value2path = {foo = box.NULL},
            exp_value2type = {foo = box.NULL},
        },
        -- The data is box.NULL.
        {
            data = box.NULL,
            vars = {},
            exp_apply_vars = box.NULL,
            exp_value2path = box.NULL,
            exp_value2type = box.NULL,
        },
        -- The data is nil.
        {
            data = nil,
            vars = {},
            exp_apply_vars = nil,
            exp_value2path = nil,
            exp_value2type = nil,
        },
    }

    for _, case in ipairs(cases) do
        local res = s:map(case.data, apply_vars, case.vars)
        t.assert_equals(res, case.exp_apply_vars)
        assert_type_equals(res, case.exp_apply_vars)

        local res = s:map(case.data, value2path)
        t.assert_equals(res, case.exp_value2path)
        assert_type_equals(res, case.exp_value2path)

        local res = s:map(case.data, value2type)
        t.assert_equals(res, case.exp_value2type)
        assert_type_equals(res, case.exp_value2type)
    end
end

-- The :map() method applies the transformation function to map
-- keys as well as for any other schema node.
--
-- Maybe it is not very obvious behavior. Moreover, there is no
-- simple way to differentiate a key and a value if the schema
-- nodes do contain specific annotations, because w.path is the
-- same for both.
--
-- For a while, hold the current behavior in the test case.
g.test_map_map_keys = function()
    local s = schema.new('myschema', schema.map({
        key = schema.scalar({type = 'string'}),
        value = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'string'}),
        }),
    }))

    local data = {foo = {bar = 'abc'}}

    -- 'foo' is replaced with 'foo', 'bar' is replaced with
    -- 'foo.bar'.
    local exp = {foo = {['foo.bar'] = 'foo.bar'}}
    local res = s:map(data, value2path)
    t.assert_equals(res, exp)
    assert_type_equals(res, exp)

    -- 'foo' and 'bar' are replaced with 'string'.
    local exp = {string = {string = 'string'}}
    local res = s:map(data, value2type)
    t.assert_equals(res, exp)
    assert_type_equals(res, exp)
end

-- Verify that the :map() method allows to transform a map
-- value.
--
-- This functionality was implemented in gh-10756.
g.test_map_map_itself = function()
    local s = schema.new('myschema', schema.map({
        key = schema.scalar({type = 'string'}),
        value = schema.scalar({type = 'string'}),
    }))

    -- This scenario performs the following transformations.
    --
    -- First, a map with one `packed` field is transformed to
    -- a map with three fields: `foo`, `bar`, `baz`.
    --
    -- These fields contain ${a}, ${b}, and ${c} placeholders,
    -- which are replaced with the provided variable values.
    --
    -- This way we verify that the traversal is pre-order.
    local vars = {
        a = 'foo',
        b = 'bar',
        c = 'baz',
    }
    local res = s:map({
        packed = '${a},${b},${c}',
    }, function(data, w, vars)
        -- Unpack a packed map.
        if w.schema.type == 'map' and data.packed ~= nil then
            assert(type(data.packed) == 'string')
            local items = data.packed:split(',')
            return {
                foo = items[1],
                bar = items[2],
                baz = items[3],
            }
        end
        -- Apply variables to string values.
        return apply_vars(data, w, vars)
    end, vars)

    t.assert_equals(res, {
        foo = 'foo',
        bar = 'bar',
        baz = 'baz',
    })
end

-- Verify :map() method on a schema with an array.
g.test_map_array = function()
    local s = schema.new('myschema', schema.array({
        items = schema.scalar({
            type = 'string',
        }),
    }))

    -- apply_vars verifies most of the logic.
    --
    -- value2path shows that w.path is correct.
    --
    -- value2type here is just for completeness.
    local cases = {
        -- There is a string item in the array.
        {
            data = {'a${b}c'},
            vars = {b = 'B'},
            exp_apply_vars = {'aBc'},
            exp_value2path = {'1'},
            exp_value2type = {'string'},
        },
        -- There is a box.NULL item in the array.
        {
            data = {box.NULL},
            vars = {},
            exp_apply_vars = {box.NULL},
            exp_value2path = {'1'},
            exp_value2type = {'cdata'},
        },
        -- The empty array.
        {
            data = {},
            vars = {},
            exp_apply_vars = {},
            exp_value2path = {},
            exp_value2type = {},
        },
        -- box.NULL as an array value.
        {
            data = box.NULL,
            vars = {},
            exp_apply_vars = box.NULL,
            exp_value2path = box.NULL,
            exp_value2type = box.NULL,
        },
        -- nil as an array value.
        {
            data = nil,
            vars = {},
            exp_apply_vars = nil,
            exp_value2path = nil,
            exp_value2type = nil,
        },
    }

    for _, case in ipairs(cases) do
        local res = s:map(case.data, apply_vars, case.vars)
        t.assert_equals(res, case.exp_apply_vars)
        assert_type_equals(res, case.exp_apply_vars)

        local res = s:map(case.data, value2path)
        t.assert_equals(res, case.exp_value2path)
        assert_type_equals(res, case.exp_value2path)

        local res = s:map(case.data, value2type)
        t.assert_equals(res, case.exp_value2type)
        assert_type_equals(res, case.exp_value2type)
    end
end

-- Verify that the :map() method allows to transform an array
-- value.
--
-- This functionality was implemented in gh-10756.
g.test_map_array_itself = function()
    local s = schema.new('myschema', schema.array({
        items = schema.scalar({type = 'string'}),
    }))

    -- This scenario performs the following transformations.
    --
    -- First, an array with one item is transformed to
    -- an with three items.
    --
    -- These items contain ${a}, ${b}, and ${c} placeholders,
    -- which are replaced with the provided variable values.
    --
    -- This way we verify that the traversal is pre-order.
    local vars = {
        a = 'foo',
        b = 'bar',
        c = 'baz',
    }
    local res = s:map({
        '${a},${b},${c}',
    }, function(data, w, vars)
        -- Unpack a packed array.
        if w.schema.type == 'array' and #data == 1 then
            assert(type(data[1]) == 'string')
            return data[1]:split(',')
        end
        -- Apply variables to string values.
        return apply_vars(data, w, vars)
    end, vars)

    t.assert_equals(res, {'foo', 'bar', 'baz'})
end

-- }}} <schema object>:map()

-- {{{ <schema object>:apply_default()

-- Basic scenario for the :apply_default() method.
g.test_apply_default = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.scalar({
            type = 'string',
            default = 'def',
        }),
    }))

    -- The value exists and shouldn't be replaced by the default.
    local data = {foo = 'bar'}
    local res = s:apply_default(data)
    local exp = {foo = 'bar'}
    t.assert_equals(res, exp)
    assert_type_equals(res, exp)

    -- No field -> replace with the default.
    local data = {}
    local res = s:apply_default(data)
    local exp = {foo = 'def'}
    t.assert_equals(res, exp)
    assert_type_equals(res, exp)

    -- box.NULL -> replace with the default (if the default
    -- exists). See also the test_apply_default_nil_default
    -- test case.
    local data = {foo = box.NULL}
    local res = s:apply_default(data)
    local exp = {foo = 'def'}
    t.assert_equals(res, exp)
    assert_type_equals(res, exp)

    -- No parent record -> create it and set the field to the
    -- default.
    local data = nil
    local res = s:apply_default(data)
    local exp = {foo = 'def'}
    t.assert_equals(res, exp)
    assert_type_equals(res, exp)

    -- Same, but with box.NULL as the marker of the missed record.
    local data = box.NULL
    local res = s:apply_default(data)
    local exp = {foo = 'def'}
    t.assert_equals(res, exp)
    assert_type_equals(res, exp)
end

-- Verify :apply_default() without the 'default' annotation.
g.test_apply_default_nil_default = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.scalar({
            type = 'string',
            -- no default annotation
        }),
    }))

    -- Keep box.NULL from the original data if the default is not
    -- provided.
    local data = {foo = box.NULL}
    local res = s:apply_default(data)
    local exp = {foo = box.NULL}
    t.assert_equals(res, exp)
    assert_type_equals(res, exp)

    -- If both the original data and the default are missed (nil),
    -- the result should be nil.
    local data = {}
    local res = s:apply_default(data)
    local exp = {}
    t.assert_equals(res, exp)
    assert_type_equals(res, exp)
end

-- Verify :apply_default() without the 'default' annotation.
g.test_apply_default_box_null_default = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.scalar({
            type = 'string',
            default = box.NULL,
        }),
    }))

    -- Replace nil in the original data with box.NULL from the
    -- default.
    local data = {}
    local res = s:apply_default(data)
    local exp = {foo = box.NULL}
    t.assert_equals(res, exp)
    assert_type_equals(res, exp)

    -- If the original value and the default are box.NULL both,
    -- the result should be box.NULL.
    local data = {foo = box.NULL}
    local res = s:apply_default(data)
    local exp = {foo = box.NULL}
    t.assert_equals(res, exp)
    assert_type_equals(res, exp)
end

-- Verify the apply_default_if annotation (do not apply cases).
g.test_apply_default_if_false = function()
    local apply_default_if_calls = {}
    local s = schema.new('myschema', schema.record({
        foo = schema.scalar({
            type = 'string',
            default = 'def',
            apply_default_if = function(data, w)
                table.insert(apply_default_if_calls, {
                    data = data,
                    schema = w.schema,
                    path = w.path,
                })
                return false
            end,
        }),
    }))

    -- The callback is not called, because the field exists.
    t.assert_equals(s:apply_default({foo = 'bar'}), {foo = 'bar'})
    t.assert_equals(apply_default_if_calls, {})

    -- The field is box.NULL, the callback is called.
    --
    -- However, the default is not applied, because the function
    -- returns false.
    t.assert_equals(s:apply_default({foo = box.NULL}), {foo = box.NULL})
    local exp = {
        {
            data = box.NULL,
            schema = s.schema.fields.foo,
            path = {'foo'},
        }
    }
    t.assert_equals(apply_default_if_calls, exp)
    assert_type_equals(apply_default_if_calls, exp)
    table.clear(apply_default_if_calls)

    -- The field is missed, the callback is called.
    --
    -- However, the default is not applied, because the function
    -- returns false.
    t.assert_equals(s:apply_default({}), {})
    local exp = {
        {
            data = nil,
            schema = s.schema.fields.foo,
            path = {'foo'},
        }
    }
    t.assert_equals(apply_default_if_calls, exp)
    assert_type_equals(apply_default_if_calls, exp)
    table.clear(apply_default_if_calls)
end

-- Verify the apply_default_if annotation (perform the apply
-- cases).
g.test_apply_default_if_true = function()
    local apply_default_if_calls = {}
    local s = schema.new('myschema', schema.record({
        foo = schema.scalar({
            type = 'string',
            default = 'def',
            apply_default_if = function(data, w)
                table.insert(apply_default_if_calls, {
                    data = data,
                    schema = w.schema,
                    path = w.path,
                })
                return true
            end,
        }),
    }))

    -- box.NULL field -> replace with the default.
    t.assert_equals(s:apply_default({foo = box.NULL}), {foo = 'def'})
    local exp = {
        {
            data = box.NULL,
            schema = s.schema.fields.foo,
            path = {'foo'},
        }
    }
    t.assert_equals(apply_default_if_calls, exp)
    assert_type_equals(apply_default_if_calls, exp)
    table.clear(apply_default_if_calls)

    -- Missed field -> replace with the default.
    t.assert_equals(s:apply_default({}), {foo = 'def'})
    local exp = {
        {
            data = nil,
            schema = s.schema.fields.foo,
            path = {'foo'},
        }
    }
    t.assert_equals(apply_default_if_calls, exp)
    assert_type_equals(apply_default_if_calls, exp)
    table.clear(apply_default_if_calls)

    -- Missed parent record -> create it and set the field to its
    -- default.
    t.assert_equals(s:apply_default(nil), {foo = 'def'})
    local exp = {
        {
            data = nil,
            schema = s.schema.fields.foo,
            path = {'foo'},
        }
    }
    t.assert_equals(apply_default_if_calls, exp)
    assert_type_equals(apply_default_if_calls, exp)
    table.clear(apply_default_if_calls)
end

-- Verify w.error() in the apply_default_if callback.
g.test_apply_default_if_error = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.scalar({
            type = 'string',
            default = 'def',
            apply_default_if = function(_data, w)
                w.error('Something went wrong in %q', 'my apply if')
            end,
        }),
    }))

    local exp_err_msg = '[myschema] foo: Something went wrong in "my apply if"'
    t.assert_error_msg_equals(exp_err_msg, function()
        local data = {}
        s:apply_default(data)
    end)
end

-- Verify that a default value is taken into account for a record.
--
-- Also verify that the record's default is applied first and only
-- then defaults are applied for its fields.
--
-- This functionality was implemented in gh-10756.
g.test_apply_default_on_record = function()
    local s = schema.new('myschema', schema.record({
        foo = schema.scalar({
            type = 'string',
            default = 'def',
        }),
        bar = schema.scalar({
            type = 'string',
            default = 'def',
        }),
    }, {
        default = {
            foo = 'foo',
        }
    }))

    local data = nil
    local res = s:apply_default(data)
    local exp = {
        foo = 'foo',
        bar = 'def',
    }
    t.assert_equals(res, exp)
end

-- Verify that a default value is taken into account for a map.
--
-- Also verify that the map's default is applied first and only
-- then defaults are applied for its fields.
--
-- This functionality was implemented in gh-10756.
g.test_apply_default_on_map = function()
    local s = schema.new('myschema', schema.map({
        key = schema.scalar({
            type = 'string',
        }),
        value = schema.scalar({
            type = 'string',
            default = 'def',
        }),
        default = {
            foo = 'foo',
            bar = box.NULL,
        }
    }))

    local data = nil
    local res = s:apply_default(data)
    local exp = {
        foo = 'foo',
        bar = 'def',
    }
    t.assert_equals(res, exp)
end

-- Verify that a default value is taken into account for an array.
--
-- Also verify that the array's default is applied first and only
-- then defaults are applied for its items.
--
-- This functionality was implemented in gh-10756.
g.test_apply_default_on_array = function()
    local s = schema.new('myschema', schema.array({
        items = schema.scalar({
            type = 'string',
            default = 'def',
        }),
        default = {'foo', box.NULL},
    }))

    local data = nil
    local res = s:apply_default(data)
    local exp = {'foo', 'def'}
    t.assert_equals(res, exp)
end

-- }}} <schema object>:apply_default()

-- {{{ Testing helpers for <schema object>:merge()

-- Verify basic rules of the merge for a two-level schema, where
-- a record or a map is on the outmost level and a scalar or an
-- array is on the inner level.
local function verify_merge(s)
    -- Determine appropriate testing values for the inner level.
    local av
    local bv
    if s.schema.type == 'record' then
        if s.schema.fields.foo.type == 'array' then
            av = {'aaa'}
            bv = {'bbb'}
        else
            av = 'aaa'
            bv = 'bbb'
        end
    elseif s.schema.type == 'map' then
        if s.schema.value.type == 'array' then
            av = {'aaa'}
            bv = {'bbb'}
        else
            av = 'aaa'
            bv = 'bbb'
        end
    else
        assert(false)
    end

    -- 1. A scalar/array field is present only in record/map A.
    -- 2. A scalar/array field is present only in record/map B.
    -- 3. A scalar/array field is present in the both
    --    records/maps.
    local a = {
        foo = av,
        bar = av,
        -- no baz
    }
    local b = {
        foo = bv,
        -- no bar
        baz = bv,
    }
    local exp = {
        foo = bv,
        bar = av,
        baz = bv,
    }
    local res = s:merge(a, b)
    t.assert_equals(res, exp)
    assert_type_equals(res, exp)

    -- nil vs box.NULL cases.
    --
    -- |     | a        | b        | res      |
    -- | --- | -------- | -------- | -------- |
    -- | foo | nil      | nil      | nil      |
    -- | bar | box.NULL | nil      | box.NULL |
    -- | baz | nil      | box.NULL | box.NULL |
    -- | fiz | box.NULL | box.NULL | box.NULL |
    local a = {
        -- no foo
        bar = box.NULL,
        -- no baz
        fiz = box.NULL,
    }
    local b = {
        -- no foo
        -- no bar
        baz = box.NULL,
        fiz = box.NULL,
    }
    local exp = {
        -- no foo
        bar = box.NULL,
        baz = box.NULL,
        fiz = box.NULL,
    }
    local res = s:merge(a, b)
    t.assert_equals(res, exp)
    assert_type_equals(res, exp)

    -- nil/box.NULL vs X ~= nil cases.
    --
    -- |     | a        | b        | res |
    -- | --- | -------- | -------- | --- |
    -- | foo | nil      | X ~= nil | X   |
    -- | bar | box.NULL | X ~= nil | X   |
    -- | baz | X ~= nil | nil      | X   |
    -- | fiz | X ~= nil | box.NULL | X   |
    local a = {
        -- no foo
        bar = box.NULL,
        baz = av,
        fiz = av,
    }
    local b = {
        foo = bv,
        bar = bv,
        -- no baz
        fiz = box.NULL,
    }
    local exp = {
        foo = bv,
        bar = bv,
        baz = av,
        fiz = av,
    }
    local res = s:merge(a, b)
    t.assert_equals(res, exp)
    assert_type_equals(res, exp)
end

-- }}} Testing helpers for <schema object>:merge()

-- {{{ <schema object>:merge()

g.test_merge_record_and_scalar = function()
    local scalar = schema.scalar({type = 'string'})
    verify_merge(schema.new('myschema', schema.record({
        foo = scalar,
        bar = scalar,
        baz = scalar,
        fiz = scalar,
    })))
end

g.test_merge_map_and_scalar = function()
    local scalar = schema.scalar({type = 'string'})
    verify_merge(schema.new('myschema', schema.map({
        key = scalar,
        value = scalar,
    })))
end

g.test_merge_record_and_array = function()
    local array = schema.array({items = schema.scalar({type = 'string'})})
    verify_merge(schema.new('myschema', schema.record({
        foo = array,
        bar = array,
        baz = array,
        fiz = array,
    })))
end

g.test_merge_map_and_array = function()
    local array = schema.array({items = schema.scalar({type = 'string'})})
    verify_merge(schema.new('myschema', schema.map({
        key = array,
        value = array,
    })))
end

-- Verify that arrays are NOT deeply merged.
--
-- We need arrays of different sizes for that.
g.test_merge_array = function()
    local s = schema.new('myschema', schema.array({
        items = schema.scalar({type = 'string'}),
    }))

    local a = {'aaa'}
    local b = {'bbb', 'ccc', 'ddd'}

    t.assert_equals(s:merge(a, b), b)
    t.assert_equals(s:merge(b, a), a)
end

-- Verify that scalars of type 'any' are deeply merged, if they're
-- tables with string keys.
--
-- This is supported after gh-10450.
g.test_merge_any_map = function()
    local s = schema.new('myschema', schema.scalar({
        type = 'any',
    }))

    local a = {foo = 'aaa'}
    local b = {bar = 'bbb'}

    local exp = {
        foo = 'aaa',
        bar = 'bbb',
    }
    t.assert_equals(s:merge(a, b), exp)
    t.assert_equals(s:merge(b, a), exp)
end

-- Verify that scalars of type 'any' are NOT deeply merged, if
-- they're arrays.
g.test_merge_any_array = function()
    local s = schema.new('myschema', schema.scalar({
        type = 'any',
    }))

    local a = {1, 2, 3}
    local b = {4, 5, 6}

    t.assert_equals(s:merge(a, b), b)
    t.assert_equals(s:merge(b, a), a)
end

-- More scenarios of merging data corresponding to an 'any'
-- scalar.
--
-- This is supported after gh-10450.
g.test_merge_any_more = function()
    local s = schema.new('myschema', schema.scalar({
        type = 'any',
    }))

    local cases = {
        -- Prefer box.NULL over nil.
        {
            a = nil,
            b = box.NULL,
            exp = box.NULL,
        },
        -- Prefer box.NULL over nil.
        {
            a = box.NULL,
            b = nil,
            exp = box.NULL,
        },
        -- Prefer X ~= nil over nil/box.NULL.
        {
            a = nil,
            b = 1,
            exp = 1,
        },
        -- Prefer X ~= nil over nil/box.NULL.
        {
            a = 1,
            b = nil,
            exp = 1,
        },
        -- Prefer X ~= nil over nil/box.NULL.
        {
            a = box.NULL,
            b = 1,
            exp = 1,
        },
        -- Prefer X ~= nil over nil/box.NULL.
        {
            a = 1,
            b = box.NULL,
            exp = 1,
        },
        -- Prefer B if A or B is not a table.
        {
            a = 1,
            b = {foo = 1},
            exp = {foo = 1},
        },
        -- Prefer B if A or B is not a table.
        {
            a = {foo = 1},
            b = 1,
            exp = 1,
        },
        -- Prefer B if A or B is a table with a non-string key.
        {
            a = {[1] = 'x'},
            b = {foo = 1},
            exp = {foo = 1},
        },
        -- Prefer B if A or B is a table with a non-string key.
        {
            a = {foo = 1},
            b = {[1] = 'x'},
            exp = {[1] = 'x'},
        },
        -- Same, but keys are string and non-string.
        {
            a = {[5] = 'x', foo = 2},
            b = {foo = 1},
            exp = {foo = 1},
        },
        -- Same, but keys are string and non-string.
        {
            a = {foo = 1},
            b = {[5] = 'x', foo = 2},
            exp = {[5] = 'x', foo = 2},
        },
        -- A deep merge.
        {
            a = {foo = {bar = 42}},
            b = {foo = {baz = 43}},
            exp = {foo = {bar = 42, baz = 43}},
        },
    }

    for _, case in ipairs(cases) do
        -- Verify on the top level.
        local a = case.a
        local b = case.b
        local exp = case.exp
        local res = s:merge(a, b)
        t.assert_equals(res, exp)
        t.assert_type(res, type(exp))

        -- The same, but nested.
        local a = {foo = case.a}
        local b = {foo = case.b}
        local exp = {foo = case.exp}
        local res = s:merge(a, b)
        t.assert_equals(res, exp)
        t.assert_type(res.foo, type(exp.foo))
    end
end

-- }}} <schema object>:merge()

-- {{{ <schema object>:pairs()

g.test_pairs = function()
    local str = schema.scalar({type = 'string'})
    local map = schema.map({
        key = schema.scalar({type = 'string'}),
        value = schema.record({
            foo = schema.scalar({type = 'number'}),
        }),
    })
    local arr = schema.array({
        items = schema.record({
            foo = schema.scalar({type = 'number'}),
        }),
    })
    local s = schema.new('myschema', schema.record({
        str = str,
        map = map,
        arr = arr,
        rec = schema.record({
            str = str,
            map = map,
            arr = arr,
            rec = schema.record({
                str = str,
                map = map,
                arr = arr,
            }),
        }),
    }))

    local res = s:pairs():map(function(w)
        return {remove_computed_fields(w.schema), w.path}
    end):totable()
    t.assert_items_equals(res, {
        {str, {'str'}},
        {map, {'map'}},
        {arr, {'arr'}},

        {str, {'rec', 'str'}},
        {map, {'rec', 'map'}},
        {arr, {'rec', 'arr'}},

        {str, {'rec', 'rec', 'str'}},
        {map, {'rec', 'rec', 'map'}},
        {arr, {'rec', 'rec', 'arr'}},
    })
end

-- }}} <schema object>:pairs()

-- {{{ schema.fromenv()

local fromenv_cases = {
    -- No value/empty value cases.
    no_value = {
        -- No schema is set to ensure that the function doesn't
        -- access the schema argument to return the result in the
        -- case. This way we're sure that it works this way for
        -- any schema.
        raw_value = nil,
        exp_value = nil,
    },
    box_null_value = {
        -- No schema is set, see above.
        raw_value = box.NULL,
        exp_value = nil,
    },
    empty_value = {
        -- No schema is set, see above.
        raw_value = '',
        exp_value = nil,
    },
    -- Scalars.
    string = {
        schema = schema.scalar({type = 'string'}),
        raw_value = 'foo',
        exp_value = 'foo',
    },
    number = {
        schema = schema.scalar({type = 'number'}),
        raw_value = '5.5',
        exp_value = 5.5,
    },
    number_negative = {
        schema = schema.scalar({type = 'number'}),
        raw_value = '-5.5',
        exp_value = -5.5,
    },
    number_error = {
        schema = schema.scalar({type = 'number'}),
        raw_value = 'foo',
        exp_err_msg = 'Unable to decode a number value from environment ' ..
            'variable "MYVAR", got "foo"',
    },
    integer = {
        schema = schema.scalar({type = 'integer'}),
        raw_value = '5',
        exp_value = 5,
    },
    integer_negative = {
        schema = schema.scalar({type = 'integer'}),
        raw_value = '-5',
        exp_value = -5,
    },
    integer_error = {
        schema = schema.scalar({type = 'integer'}),
        raw_value = '5.5',
        exp_err_msg = 'Unable to decode an integer value from environment ' ..
            'variable "MYVAR", got "5.5"',
    },
    boolean_0 = {
        schema = schema.scalar({type = 'boolean'}),
        raw_value = '0',
        exp_value = false,
    },
    boolean_false = {
        schema = schema.scalar({type = 'boolean'}),
        raw_value = 'false',
        exp_value = false,
    },
    boolean_false_uppercase = {
        schema = schema.scalar({type = 'boolean'}),
        raw_value = 'FALSE',
        exp_value = false,
    },
    boolean_1 = {
        schema = schema.scalar({type = 'boolean'}),
        raw_value = '1',
        exp_value = true,
    },
    boolean_true = {
        schema = schema.scalar({type = 'boolean'}),
        raw_value = 'true',
        exp_value = true,
    },
    boolean_true_uppercase = {
        schema = schema.scalar({type = 'boolean'}),
        raw_value = 'TRUE',
        exp_value = true,
    },
    any_string = {
        schema = schema.scalar({type = 'any'}),
        raw_value = '"foo"',
        exp_value = 'foo',
    },
    any_number = {
        schema = schema.scalar({type = 'any'}),
        raw_value = '5.5',
        exp_value = 5.5,
    },
    any_object = {
        schema = schema.scalar({type = 'any'}),
        raw_value = '{"foo": "bar"}',
        exp_value = {foo = 'bar'},
    },
    any_array = {
        schema = schema.scalar({type = 'any'}),
        raw_value = '[1, 2, 3]',
        exp_value = {1, 2, 3},
    },
    any_error = {
        schema = schema.scalar({type = 'any'}),
        raw_value = 'foo',
        exp_err_msg = 'Unable to decode JSON data in environment variable ' ..
            '"MYVAR": Expected value but found invalid token on line 1 at ' ..
            'character 1 here \' >> foo\'',
    },
    -- TODO: Remove when a union node will be implemented.
    number_string_pass_number = {
        schema = schema.scalar({type = 'number, string'}),
        raw_value = '-4.7',
        exp_value = -4.7,
    },
    number_string_pass_string = {
        schema = schema.scalar({type = 'number, string'}),
        raw_value = 'foo',
        exp_value = 'foo',
    },
    string_number_pass_number = {
        schema = schema.scalar({type = 'string, number'}),
        raw_value = '-4.7',
        exp_value = -4.7,
    },
    string_number_pass_string = {
        schema = schema.scalar({type = 'string, number'}),
        raw_value = 'foo',
        exp_value = 'foo',
    },
    -- Composite types.
    record_error = {
        schema = schema.record({foo = schema.scalar({type = 'string'})}),
        raw_value = '{"foo": "bar"}',
        exp_err_msg = 'Attempt to parse environment variable "MYVAR" as a ' ..
            'record value: this is not supported yet and likely caused by ' ..
            'an internal error in the config module',
    },
    map_simple_string = {
        schema = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'string'}),
        }),
        raw_value = 'foo=bar,baz=fiz',
        exp_value = {foo = 'bar', baz = 'fiz'},
    },
    map_simple_number = {
        schema = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'number'}),
        }),
        raw_value = 'foo=5.5,baz=-4.7',
        exp_value = {foo = 5.5, baz = -4.7},
    },
    map_simple_integer = {
        schema = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'integer'}),
        }),
        raw_value = 'foo=5,baz=-4',
        exp_value = {foo = 5, baz = -4},
    },
    map_simple_boolean = {
        schema = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'boolean'}),
        }),
        raw_value = 'foo=true,baz=0',
        exp_value = {foo = true, baz = false},
    },
    map_simple_any_error = {
        schema = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'any'}),
        }),
        raw_value = 'foo="bar",baz="fiz"',
        exp_err_msg = 'Use the JSON object format for environment variable ' ..
            '"MYVAR": the field values are supposed to have an arbitrary ' ..
            'type ("any") that is not supported by the simple ' ..
            '"foo=bar,baz=fiz" object format. A JSON object value starts ' ..
            'from "{".',
    },
    map_simple_composite_value_error = {
        schema = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.record({}),
        }),
        raw_value = 'foo={},bar={}',
        exp_err_msg = 'Use the JSON object format for environment variable ' ..
            '"MYVAR": the field values are supposed to have a composite ' ..
            'type ("record") that is not supported by the simple ' ..
            '"foo=bar,baz=fiz" object format. A JSON object value starts ' ..
            'from "{".',
    },
    map_simple_nonstring_key_error = {
        schema = schema.map({
            key = schema.scalar({type = 'number'}),
            value = schema.scalar({type = 'string'}),
        }),
        raw_value = '1=foo,2=bar',
        exp_err_msg = 'Use the JSON object format for environment variable ' ..
            '"MYVAR": the keys are supposed to have a non-string type ' ..
            '("number") that is not supported by the simple ' ..
            '"foo=bar,baz=fiz" object format. A JSON object value starts ' ..
            'from "{".',
    },
    map_simple_noeq_error = {
        schema = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'string'}),
        }),
        raw_value = 'foo=bar,baz',
        exp_err_msg = 'Unable to decode data in environment variable ' ..
            '"MYVAR" assuming the simple "foo=bar,baz=fiz" object format: ' ..
            'no "=" is found in a key-value pair. Use either the simple ' ..
            '"foo=bar,baz=fiz" object format or the JSON object format ' ..
            '(starts from "{").',
    },
    map_simple_lhs_error = {
        schema = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'string'}),
        }),
        raw_value = 'foo=bar,=baz',
        exp_err_msg = 'Unable to decode data in environment variable ' ..
            '"MYVAR" assuming the simple "foo=bar,baz=fiz" object format: ' ..
            'no value before "=" is found in a key-value pair. Use either ' ..
            'the simple "foo=bar,baz=fiz" object format or the JSON object ' ..
            'format (starts from "{").',
    },
    map_json_string = {
        schema = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'string'}),
        }),
        raw_value = '{"foo": "bar", "baz": "fiz"}',
        exp_value = {foo = 'bar', baz = 'fiz'},
    },
    map_json_any = {
        schema = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'any'}),
        }),
        raw_value = '{"foo": ["bar", 42], "baz": "fiz"}',
        exp_value = {foo = {'bar', 42}, baz = 'fiz'},
    },
    map_json_record = {
        schema = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.record({
                bar = schema.scalar({type = 'string'}),
            }),
        }),
        raw_value = '{"foo": {"bar": "baz"}}',
        exp_value = {foo = {bar = 'baz'}},
    },
    map_json_array_error_1 = {
        schema = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'string'}),
        }),
        raw_value = '[]',
        exp_err_msg = 'A JSON array is provided for environment variable ' ..
            '"MYVAR" of type map, an object is expected. Use either the ' ..
            'simple "foo=bar,baz=fiz" object format or the JSON object ' ..
            'format (starts from "{").',
    },
    map_json_array_error_2 = {
        schema = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'any'}),
        }),
        raw_value = '[]',
        exp_err_msg = 'A JSON array is provided for environment variable ' ..
            '"MYVAR" of type map, an object is expected. Use the JSON ' ..
            'object format (starts from "{").',
    },
    map_json_invalid = {
        schema = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'string'}),
        }),
        raw_value = '{"foo": "bar"',
        exp_err_msg = 'Unable to decode JSON data in environment variable ' ..
            '"MYVAR": Expected comma or \'}\' but found end on line 1 at ' ..
            'character 14 here \'": "bar" >> \'',
    },
    array_simple_string = {
        schema = schema.array({
            items = schema.scalar({type = 'string'}),
        }),
        raw_value = 'foo,bar,baz',
        exp_value = {'foo', 'bar', 'baz'},
    },
    array_simple_number = {
        schema = schema.array({
            items = schema.scalar({type = 'number'}),
        }),
        raw_value = '5.5,-4.7,0',
        exp_value = {5.5, -4.7, 0},
    },
    array_simple_integer = {
        schema = schema.array({
            items = schema.scalar({type = 'integer'}),
        }),
        raw_value = '5,-4,0',
        exp_value = {5, -4, 0},
    },
    array_simple_boolean = {
        schema = schema.array({
            items = schema.scalar({type = 'boolean'}),
        }),
        raw_value = 'true,false,1,0',
        exp_value = {true, false, true, false},
    },
    array_simple_any_error = {
        schema = schema.array({
            items = schema.scalar({type = 'any'}),
        }),
        raw_value = '"foo","bar","baz"',
        exp_err_msg = 'Use the JSON array format for environment variable ' ..
            '"MYVAR": the item values are supposed to have an arbitrary ' ..
            'type ("any") that is not supported by the simple ' ..
            '"foo,bar,baz" array format. A JSON array value starts ' ..
            'from "[".',
    },
    array_simple_composite_items_error = {
        schema = schema.array({
            items = schema.record({}),
        }),
        raw_value = 'foo',
        exp_err_msg = 'Use the JSON array format for environment variable ' ..
            '"MYVAR": the item values are supposed to have a composite ' ..
            'type ("record") that is not supported by the simple ' ..
            '"foo,bar,baz" array format. A JSON array value starts ' ..
            'from "[".',
    },
    array_json_string = {
        schema = schema.array({
            items = schema.scalar({type = 'string'}),
        }),
        raw_value = '["foo", "bar", "baz"]',
        exp_value = {'foo', 'bar', 'baz'},
    },
    array_json_any = {
        schema = schema.array({
            items = schema.scalar({type = 'any'}),
        }),
        raw_value = '[{"foo": 42}, ["bar", 42], "baz", 43]',
        exp_value = {{foo = 42}, {'bar', 42}, 'baz', 43},
    },
    arrap_json_record = {
        schema = schema.array({
            items = schema.record({
                foo = schema.scalar({type = 'string'}),
            }),
        }),
        raw_value = '[{"foo": "bar"}]',
        exp_value = {{foo = 'bar'}},
    },
    array_json_map_error_1 = {
        schema = schema.array({
            items = schema.scalar({type = 'string'}),
        }),
        raw_value = '{}',
        exp_err_msg = 'A JSON object is provided for environment variable ' ..
            '"MYVAR" of type array, an array is expected. Use either the ' ..
            'simple "foo,bar,baz" array format or the JSON array ' ..
            'format (starts from "[").',
    },
    array_json_map_error_2 = {
        schema = schema.array({
            items = schema.scalar({type = 'any'}),
        }),
        raw_value = '{}',
        exp_err_msg = 'A JSON object is provided for environment variable ' ..
            '"MYVAR" of type array, an array is expected. Use the JSON ' ..
            'array format (starts from "[").',
    },
    array_json_invalid = {
        schema = schema.array({
            items = schema.scalar({type = 'string'}),
        }),
        raw_value = '["foo", "bar"',
        exp_err_msg = 'Unable to decode JSON data in environment variable ' ..
            '"MYVAR": Expected comma or \']\' but found end on line 1 at ' ..
            'character 14 here \'", "bar" >> \'',
    },
}

for case_name, case in pairs(fromenv_cases) do
    g['test_fromenv_' .. case_name] = function()
        if case.exp_err_msg == nil then
            local res = schema.fromenv('MYVAR', case.raw_value, case.schema)
            t.assert_equals(res, case.exp_value)
            assert_type_equals(res, case.exp_value)
        else
            assert(case.exp_value == nil)
            t.assert_error_msg_equals(case.exp_err_msg, function()
                schema.fromenv('MYVAR', case.raw_value, case.schema)
            end)
        end
    end
end

-- }}} schema.fromenv()
