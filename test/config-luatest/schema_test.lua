local ffi = require('ffi')
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
