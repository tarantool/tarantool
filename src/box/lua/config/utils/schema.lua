-- Schema-aware data manipulations.
--
-- The following annotations have some meaning for the module
-- itself (affects work of the schema methods):
--
-- * allowed_values
-- * validate
-- * default
-- * apply_default_if
--
-- Others are just stored.

local fun = require('fun')
local json = require('json')

local methods = {}
local schema_mt = {}

local scalars = {}

-- {{{ Walkthrough helpers

-- Create walkthrough context.
local function walkthrough_start(self, params)
    local ctx = {path = {}, name = rawget(self, 'name')}
    for k, v in pairs(params or {}) do
        ctx[k] = v
    end
    return ctx
end

-- Step down to a field.
local function walkthrough_enter(ctx, name)
    table.insert(ctx.path, name)
end

-- Step up from the last field.
local function walkthrough_leave(ctx)
    table.remove(ctx.path)
end

-- Construct a string that describes the current path.
local function walkthrough_path(ctx)
    local res = ''
    for _, name in ipairs(ctx.path) do
        if type(name) == 'number' then
            res = res .. ('[%d]'):format(name)
        else
            res = res .. '.' .. name
        end
    end
    if res:startswith('.') then
        res = res:sub(2)
    end
    return res
end

-- Generate a prefix for an error message based on the given
-- walkthrough context.
local function walkthrough_error_prefix(ctx)
    if ctx.path == nil or next(ctx.path) == nil then
        return ('[%s] '):format(ctx.name)
    end
    return ('[%s] %s: '):format(ctx.name, walkthrough_path(ctx))
end

-- Generate an error supplemented by details from the given
-- walkthrough context.
local function walkthrough_error(ctx, message, ...)
    local error_prefix = walkthrough_error_prefix(ctx)
    error(('%s%s'):format(error_prefix, message:format(...)), 0)
end

-- Return a function that raises an error with a prefix formed
-- from the given context.
--
-- The context information is gathered before the capturing.
local function walkthrough_error_capture(ctx)
    local error_prefix = walkthrough_error_prefix(ctx)
    return function(message, ...)
        error(('%s%s'):format(error_prefix, message:format(...)), 0)
    end
end

-- Verify that the data is a table and, if it is not so, produce a
-- nice schema-aware error.
--
-- Applicable for a record, a map, an array.
--
-- Useful as part of validation, but also as a lightweight
-- consistency check.
local function walkthrough_assert_table(ctx, schema, data)
    assert(schema.type == 'record' or schema.type == 'map' or
        schema.type == 'array')

    if type(data) == 'table' then
        return
    end

    local article = schema.type == 'array' and 'an' or 'a'
    walkthrough_error(ctx, 'Unexpected data type for %s %s: %q', article,
        schema.type, type(data))
end

-- }}} Walkthrough helpers

-- {{{ Scalar definitions

-- A scalar definition:
--
-- {
--     -- How the scalar is named.
--     type = <string>,
--     -- Check given data against the type constraints.
--
--     -> true (means the data is valid)
--     -> false, err (otherwise)
--     validate_noexc = <function>,
--
--     -- Parse data originated from an environment variable.
--     --
--     -- Should raise an error it is not possible to interpret
--     -- the data as a value of the given scalar type.
--     --
--     -- Return a parsed/typecasted value of the given scalar
--     -- type otherwise.
--     fromenv = <function>,
-- }

-- Verify whether the given value (data) has expected type and
-- produce a human readable error message otherwise.
local function validate_type_noexc(data, exp_type)
    -- exp_type is like {'string', 'number'}.
    if type(exp_type) == 'table' then
        local found = false
        for _, exp_t in ipairs(exp_type) do
            if type(data) == exp_t then
                found = true
                break
            end
        end
        if not found then
            local exp_type_str = ('"%s"'):format(table.concat(exp_type, '", "'))
            local err = ('Expected one of %s, got %q'):format(exp_type_str,
                type(data))
            return false, err
        end
        return true
    end

    -- exp_type is a Lua type like 'string'.
    assert(type(exp_type) == 'string')
    if type(data) ~= exp_type then
        local err = ('Expected %q, got %q'):format(exp_type, type(data))
        return false, err
    end
    return true
end

scalars.string = {
    type = 'string',
    validate_noexc = function(data)
        return validate_type_noexc(data, 'string')
    end,
    fromenv = function(_env_var_name, raw_value)
        return raw_value
    end,
}

scalars.number = {
    type = 'number',
    validate_noexc = function(data)
        -- TODO: Should we accept cdata<int64_t> and
        -- cdata<uint64_t> here?
        return validate_type_noexc(data, 'number')
    end,
    fromenv = function(env_var_name, raw_value)
        -- TODO: Accept large integers and return cdata<int64_t>
        -- or cdata<uint64_t>?
        local res = tonumber(raw_value)
        if res == nil then
            error(('Unable to decode a number value from environment ' ..
                'variable %q, got %q'):format(env_var_name, raw_value), 0)
        end
        return res
    end,
}

-- TODO: This hack is needed until a union schema node will be
-- implemented.
scalars['string, number'] = {
    type = 'string, number',
    validate_noexc = function(data)
        return validate_type_noexc(data, {'string', 'number'})
    end,
    fromenv = function(_env_var_name, raw_value)
        return tonumber(raw_value) or raw_value
    end,
}
scalars['number, string'] = {
    type = 'number, string',
    validate_noexc = function(data)
        return validate_type_noexc(data, {'string', 'number'})
    end,
    fromenv = function(_env_var_name, raw_value)
        return tonumber(raw_value) or raw_value
    end,
}

scalars.integer = {
    type = 'integer',
    validate_noexc = function(data)
        -- TODO: Accept cdata<int64_t> and cdata<uint64_t>.
        local ok, err = validate_type_noexc(data, 'number')
        if not ok then
            return false, err
        end
        if data - math.floor(data) ~= 0 then
            -- NB: %s is chosen deliberately: it formats a
            -- floating-point number in a more human friendly way
            -- than %f. For example, 5.5 vs 5.500000.
            local err = ('Expected number without a fractional part, ' ..
                'got %s'):format(data)
            return false, err
        end
        return true
    end,
    fromenv = function(env_var_name, raw_value)
        local res = tonumber64(raw_value)
        if res == nil then
            error(('Unable to decode an integer value from environment ' ..
                'variable %q, got %q'):format(env_var_name, raw_value), 0)
        end
        return res
    end,
}

scalars.boolean = {
    type = 'boolean',
    validate_noexc = function(data)
        return validate_type_noexc(data, 'boolean')
    end,
    fromenv = function(env_var_name, raw_value)
        -- Accept false/true case insensitively.
        --
        -- Accept 0/1 as boolean values.
        if raw_value:lower() == 'false' or raw_value == '0' then
            return false
        end
        if raw_value:lower() == 'true' or raw_value == '1' then
            return true
        end

        error(('Unable to decode a boolean value from environment ' ..
            'variable %q, got %q'):format(env_var_name, raw_value), 0)
    end,
}

scalars.any = {
    type = 'any',
    validate_noexc = function(_data)
        -- No validation.
        return true
    end,
    fromenv = function(env_var_name, raw_value)
        -- Don't autoguess type. Accept JSON only.
        local ok, res = pcall(json.decode, raw_value)
        if not ok then
            error(('Unable to decode JSON data in environment ' ..
                'variable %q: %s'):format(env_var_name, res), 0)
        end
        return res
    end,
}

local function is_scalar(schema)
    return scalars[schema.type] ~= nil
end

-- }}} Scalar definitions

-- {{{ Schema node constructors: scalar, record, map, array

-- A schema node:
--
-- {
--     -- One of scalar types, 'record', 'map' or 'array'.
--     type = <string>,
--     -- For a record.
--     fields = <table>,
--     -- For a map.
--     key = <table>,
--     value = <table>,
--     -- For an array.
--     items = <table>,
--     -- Arbitrary user specified annotations.
--     <..annotations..>
-- }

-- Create a scalar.
--
-- Example:
--
-- schema.scalar({
--     type = 'string',
--     <..annotations..>,
-- })
local function scalar(scalar_def)
    assert(scalar_def.type ~= nil)
    assert(is_scalar(scalar_def))
    return scalar_def
end

-- Create a record.
--
-- A record node describes an object with the following properties:
--
-- * string keys
-- * certain keys (listed)
-- * certain value types (listed)
--
-- Example:
--
-- schema.record({
--     foo = <schema node>,
--     bar = <schema node>,
-- }, {
--     <..annotations..>
-- })
local function record(fields, annotations)
    local res = {
        type = 'record',
        fields = fields or {},
    }
    for k, v in pairs(annotations or {}) do
        assert(k ~= 'type' and k ~= 'fields')
        res[k] = v
    end
    return res
end

-- Create a map.
--
-- A map node describes an object with the following properties:
--
-- * arbitrary keys
-- * all keys have the same certain type
-- * all values have the same certain type
--
-- Example:
--
-- schema.map({
--     key = <schema node>,
--     value = <schema node>,
--     <..annotations..>
-- })
local function map(map_def)
    assert(map_def.key ~= nil)
    assert(map_def.value ~= nil)
    assert(map_def.type == nil)
    local res = table.copy(map_def)
    res.type = 'map'
    return res
end

-- Create an array.
--
-- Example:
--
-- schema.array({
--     items = <schema node>,
--     <..annotations..>
-- })
local function array(array_def)
    assert(array_def.items ~= nil)
    assert(array_def.type == nil)
    local res = table.copy(array_def)
    res.type = 'array'
    return res
end

-- }}} Schema node constructors: scalar, record, map, array

-- {{{ Derived schema node type constructors: enum, set

local function validate_no_repeat(data, w)
    local visited = {}
    for _, item in ipairs(data) do
        assert(type(item) == 'string')
        if visited[item] then
            w.error('Values should be unique, but %q appears at ' ..
                'least twice', item)
        end
        visited[item] = true
    end
end

-- Shortcut for a string scalar with given allowed values.
local function enum(allowed_values, annotations)
    local scalar_def = {
        type = 'string',
        allowed_values = allowed_values,
    }
    for k, v in pairs(annotations or {}) do
        assert(k ~= 'type' and k ~= 'allowed_values')
        scalar_def[k] = v
    end
    return scalar(scalar_def)
end

-- Shortcut for array of unique string values from the given list
-- of allowed values.
local function set(allowed_values, annotations)
    local array_def = {
        items = enum(allowed_values),
        validate = validate_no_repeat,
    }
    for k, v in pairs(annotations or {}) do
        assert(k ~= 'type' and k ~= 'items' and k ~= 'validate')
        array_def[k] = v
    end
    return array(array_def)
end

-- }}} Derived schema node type constructors: enum, set

-- {{{ <schema object>:validate()

-- Verify that the given table adheres array requirements.
--
-- It accepts an array without holes.
--
-- Strictly speaking,
--
-- * If the table is empty it is OK.
-- * If the table is non-empty, the constraints are the following:
--   * all keys are numeric, without a fractional part
--   * the lower key is 1
--   * the higher key is equal to the number of items
local function validate_table_is_array(data, ctx)
    assert(type(data) == 'table')

    -- Check that all the keys are numeric.
    local key_count = 0
    local min_key = 1/0  -- +inf
    local max_key = -1/0 -- -inf
    for k, _ in pairs(data) do
        if type(k) ~= 'number' then
            walkthrough_error(ctx, 'An array contains a non-numeric ' ..
                'key: %q', k)
        end
        if k - math.floor(k) ~= 0 then
            walkthrough_error(ctx, 'An array contains a non-integral ' ..
                'numeric key: %s', k)
        end
        key_count = key_count + 1
        min_key = math.min(min_key, k)
        max_key = math.max(max_key, k)
    end

    -- An empty array is a valid array.
    if key_count == 0 then
        return
    end

    -- Check that the array starts from 1 and has no holes.
    if min_key ~= 1 then
        walkthrough_error(ctx, 'An array must start from index 1, ' ..
            'got min index %d', min_key)
    end

    -- Check that the array has no holes.
    if max_key ~= key_count then
        walkthrough_error(ctx, 'An array must not have holes, got ' ..
            'a table with %d integer fields with max index %d', key_count,
            max_key)
    end
end

-- Verify the given data against the `allowed_values` annotation
-- in the schema node (if present).
local function validate_by_allowed_values(schema, data, ctx)
    if schema.allowed_values == nil then
        return
    end

    assert(type(schema.allowed_values) == 'table')
    local found = false
    for _, v in ipairs(schema.allowed_values) do
        if data == v then
            found = true
            break
        end
    end
    if not found then
        walkthrough_error(ctx, 'Got %s, but only the following values ' ..
            'are allowed: %s', data, table.concat(schema.allowed_values, ', '))
    end
end

-- Call schema node specific validation function.
local function validate_by_node_function(schema, data, ctx)
    if schema.validate == nil then
        return
    end

    assert(type(schema.validate) == 'function')
    local w = {
        schema = schema,
        -- ctx.path is modified during the traversal, so an
        -- attempt to store it and use later would give
        -- an unexpected result. Let's copy it to avoid the
        -- confusion.
        path = table.copy(ctx.path),
        error = walkthrough_error_capture(ctx),
    }
    schema.validate(data, w)
end

local function validate_impl(schema, data, ctx)
    if is_scalar(schema) then
        local scalar_def = scalars[schema.type]
        assert(scalar_def ~= nil)

        local ok, err = scalar_def.validate_noexc(data)
        if not ok then
            walkthrough_error(ctx, 'Unexpected data for scalar %q: %s',
                schema.type, err)
        end
    elseif schema.type == 'record' then
        walkthrough_assert_table(ctx, schema, data)

        for field_name, field_def in pairs(schema.fields) do
            walkthrough_enter(ctx, field_name)
            local field = data[field_name]
            -- Assume fields as non-required.
            if field ~= nil then
                validate_impl(field_def, field, ctx)
            end
            walkthrough_leave(ctx)
        end

        -- Walk over the data to catch unknown fields.
        for field_name, _ in pairs(data) do
            local field_def = schema.fields[field_name]
            if field_def == nil then
                walkthrough_error(ctx, 'Unexpected field %q', field_name)
            end
        end
    elseif schema.type == 'map' then
        walkthrough_assert_table(ctx, schema, data)

        for field_name, field_value in pairs(data) do
            walkthrough_enter(ctx, field_name)
            validate_impl(schema.key, field_name, ctx)
            validate_impl(schema.value, field_value, ctx)
            walkthrough_leave(ctx)
        end
    elseif schema.type == 'array' then
        walkthrough_assert_table(ctx, schema, data)
        validate_table_is_array(data, ctx)

        for i, v in ipairs(data) do
            walkthrough_enter(ctx, i)
            validate_impl(schema.items, v, ctx)
            walkthrough_leave(ctx)
        end
    else
        assert(false)
    end

    validate_by_allowed_values(schema, data, ctx)

    -- Call schema node specific validation function.
    --
    -- Important: it is called when all the type validation is
    -- already done, including nested nodes.
    validate_by_node_function(schema, data, ctx)
end

-- Validate the given data against the given schema.
--
-- Nuances:
--
-- * `schema.new('<...>', schema.scalar(<...>))` doesn't accept
--   `nil` and `box.NULL`. However,
-- * All fields in a record are optional: they accept `nil` and
--   `box.NULL`.
-- * The record/map/array determination is purely schema based.
--   mt.__serialize marks in the data are not involved anyhow.
-- * An array shouldn't have any holes (nil values in a middle).
--
-- Annotations taken into accounts:
--
-- * allowed_values (table) -- whitelist of values
-- * validate (function) -- schema node specific validator
--
--   validate = function(data, w)
--       -- w.schema -- current schema node
--       -- w.path -- path to the node
--       -- w.error -- function that prepends a caller provided
--       --            error message with context information;
--       --            use it for nice error messages
--   end
function methods.validate(self, data)
    local ctx = walkthrough_start(self)
    validate_impl(rawget(self, 'schema'), data, ctx)
end

-- }}} <schema object>:validate()

-- {{{ :get()/:set() helpers

-- The path can be passed as a string in the dot notation or as a
-- table representing an array of components. This function
-- converts the path into the array if necessary.
local function normalize_path(path, error_f)
    if type(path) ~= 'string' and type(path) ~= 'table' then
        return error_f()
    end

    -- Dot notation/JSON path alike.
    --
    -- TODO: Support numeric indexing: 'foo[1]' and `{'foo', 1}`.
    if type(path) == 'string' then
        -- NB: string.split('') returns {''}.
        if path == '' then
            path = {}
        else
            path = path:split('.')
        end
    end

    return path
end

-- }}} :get()/:set() helpers

-- {{{ <schema object>:get()

local function get_usage()
    error('Usage: schema:get(data: <as defined by the schema>, ' ..
        'path: nil/string/table)', 0)
end

local function get_impl(schema, data, ctx)
    -- The journey is finished. Return what is under the feet.
    if #ctx.journey == 0 then
        return data
    end

    -- There are more steps in the journey (at least one).
    -- Let's dive deeper and process it per schema node type.

    local requested_field = ctx.journey[1]
    assert(requested_field ~= nil)

    if is_scalar(schema) then
        walkthrough_error(ctx, 'Attempt to index a scalar value of type %s ' ..
            'by field %q', schema.type, requested_field)
    elseif schema.type == 'record' then
        walkthrough_enter(ctx, requested_field)
        local field_def = schema.fields[requested_field]
        if field_def == nil then
            walkthrough_error(ctx, 'No such field in the schema')
        end

        -- Even if there is no such field in the data, continue
        -- the descending to validate the path against the schema.
        local field_value
        if data ~= nil then
            field_value = data[requested_field]
        end

        table.remove(ctx.journey, 1)
        return get_impl(field_def, field_value, ctx)
    elseif schema.type == 'map' then
        walkthrough_enter(ctx, requested_field)
        local field_def = schema.value

        -- Even if there is no such field in the data, continue
        -- the descending to validate the path against the schema.
        local field_value
        if data ~= nil then
            field_value = data[requested_field]
        end

        table.remove(ctx.journey, 1)
        return get_impl(field_def, field_value, ctx)
    elseif schema.type == 'array' then
        -- TODO: Support 'foo[1]' and `{'foo', 1}` paths. See the
        -- normalize_path() function.
        walkthrough_error(ctx, 'Indexing an array is not supported yet')
    else
        assert(false)
    end
end

-- Get nested data that is pointed by the given path.
--
-- Important: the data is assumed as already validated against
-- the given schema.
--
-- The indexing is performed in the optional chaining manner
-- ('foo.bar' works like foo?.bar in TypeScript).
--
-- The function checks the path against the schema: it doesn't
-- allow to use a non-existing field or index a scalar value.
--
-- The path is either array-like table or a string in the dot
-- notation.
--
-- local data = {foo = {bar = 'x'}}
-- schema:get(data, 'foo.bar') -> 'x'
-- schema:get(data, {'foo', 'bar'}) -> 'x'
--
-- local data = {}
-- schema:get(data, 'foo.bar') -> nil
--
-- Nuances:
--
-- * Array indexing is not supported yet.
-- * A scalar of the 'any' type can't be indexed, even when it is
--   a table. It is OK to acquire the whole value of the 'any'
--   type.
function methods.get(self, data, path)
    local schema = rawget(self, 'schema')

    -- It is easy to forget about the `data` argument or misorder
    -- the `data` and the `path` arguments. Let's add a fast check
    -- for the most common scenario: a record schema and a string
    -- path.
    if schema.type == 'record' and type(data) ~= 'table' then
        return get_usage()
    end

    if path ~= nil then
        path = normalize_path(path, get_usage)
    end

    if path == nil or next(path) == nil then
        return data
    end

    local ctx = walkthrough_start(self, {
        -- The `path` field is already in the context and it means
        -- the passed path. Let's name the remaining path as
        -- `journey`.
        journey = path,
    })
    return get_impl(schema, data, ctx)
end

-- }}} <schema object>:get()

-- {{{ <schema object>:set()

local function set_usage()
    error('Usage: schema:set(data: <as defined by the schema>, ' ..
        'path: string/table, rhs: <as defined by the schema>)', 0)
end

local function set_impl(schema, data, rhs, ctx)
    -- The journey is finished. Validate and return the new value.
    if #ctx.journey == 0 then
        -- Call validate_impl() directly to don't construct a
        -- schema object.
        validate_impl(schema, rhs, ctx)
        return rhs
    end

    local requested_field = ctx.journey[1]
    assert(requested_field ~= nil)

    if is_scalar(schema) then
        walkthrough_error(ctx, 'Attempt to index a scalar value of type %s ' ..
            'by field %q', schema.type, requested_field)
    elseif schema.type == 'record' then
        walkthrough_enter(ctx, requested_field)
        local field_def = schema.fields[requested_field]
        if field_def == nil then
            walkthrough_error(ctx, 'No such field in the schema')
        end

        walkthrough_assert_table(ctx, schema, data)
        local field_value = data[requested_field] or {}
        table.remove(ctx.journey, 1)
        data[requested_field] = set_impl(field_def, field_value, rhs, ctx)
        return data
    elseif schema.type == 'map' then
        walkthrough_enter(ctx, requested_field)
        local field_def = schema.value

        walkthrough_assert_table(ctx, schema, data)
        local field_value = data[requested_field] or {}
        table.remove(ctx.journey, 1)
        data[requested_field] = set_impl(field_def, field_value, rhs, ctx)
        return data
    elseif schema.type == 'array' then
        -- TODO: Support 'foo[1]' and `{'foo', 1}` paths. See the
        -- normalize_path() function.
        walkthrough_error(ctx, 'Indexing an array is not supported yet')
    else
        assert(false)
    end
end

-- Set the given `rhs` value at the given path in the `data`.
--
-- Important: `data` is assumed as already validated against the
-- given schema, but `rhs` is validated by the method before the
-- assignment.
--
-- The function checks the path against the schema: it doesn't
-- allow to use a non-existing field or index a scalar value.
--
-- The path is either array-like table or a string in the dot
-- notation.
--
-- local data = {}
-- schema:set(data, 'foo.bar', 42)
-- print(data.foo.bar) -- 42
--
-- local data = {}
-- schema:set(data, {'foo', 'bar'}, 42)
-- print(data.foo.bar) -- 42
--
-- Nuances:
--
-- * A root node (pointed by the empty path) can't be set using
--   this method.
-- * Array indexing is not supported yet.
-- * A scalar of the 'any' type can't be indexed, even when it is
--   a table. It is OK to set the whole value of the 'any'
--   type.
function methods.set(self, data, path, rhs)
    local schema = rawget(self, 'schema')

    -- Detect `data` and `path` misordering for the most common
    -- scenario: a record schema and a string path.
    if schema.type == 'record' and type(data) ~= 'table' then
        return set_usage()
    end

    if path ~= nil then
        path = normalize_path(path, set_usage)
    end

    if path == nil or next(path) == nil then
        error('schema:set: empty path', 0)
    end

    local ctx = walkthrough_start(self, {
        -- The `path` field is already in the context and it means
        -- the passed path. Let's name the remaining path as
        -- `journey`.
        journey = path,
    })
    return set_impl(schema, data, rhs, ctx)
end

-- }}} <schema object>:set()

-- {{{ <schema object>:filter()

local function filter_impl(schema, data, f, ctx)
    local w = {
        path = table.copy(ctx.path),
        schema = schema,
        data = data,
    }
    if f(w) then
        table.insert(ctx.acc, w)
    end

    -- The exit condition is after the table.insert(), because a
    -- caller may want to handle box.NULL values somehow.
    if data == nil then
        return
    end

    -- luacheck: ignore 542 empty if branch
    if is_scalar(schema) then
        -- Nothing to do.
    elseif schema.type == 'record' then
        walkthrough_assert_table(ctx, schema, data)

        for field_name, field_def in pairs(schema.fields) do
            walkthrough_enter(ctx, field_name)
            local field_value = data[field_name]
            -- Step down if the field exists in the data. box.NULL
            -- field value is interpreted here as an existing
            -- field.
            if type(field_value) ~= 'nil' then
                filter_impl(field_def, field_value, f, ctx)
            end
            walkthrough_leave(ctx)
        end
    elseif schema.type == 'map' then
        walkthrough_assert_table(ctx, schema, data)

        for field_name, field_value in pairs(data) do
            walkthrough_enter(ctx, field_name)
            filter_impl(schema.key, field_name, f, ctx)
            filter_impl(schema.value, field_value, f, ctx)
            walkthrough_leave(ctx)
        end
    elseif schema.type == 'array' then
        walkthrough_assert_table(ctx, schema, data)

        for i, v in ipairs(data) do
            walkthrough_enter(ctx, i)
            filter_impl(schema.items, v, f, ctx)
            walkthrough_leave(ctx)
        end
    else
        assert(false)
    end
end

-- Filter data based on the schema annotations.
--
-- Important: the data is assumed as already validated against
-- the given schema. (A fast type check is performed on composite
-- types, but it is not recommended to lean on it.)
--
-- The user-provided filter function `f` receives the following
-- table as the argument:
--
-- w = {
--     path = <array-like table>,
--     schema = <schema node>,
--     data = <data at the given path>,
-- }
--
-- And returns a boolean value that is interpreted as 'accepted'
-- or 'not accepted'.
--
-- The user-provided function `f` is called for each schema node,
-- including ones that have box.NULL value (but not nil). A node
-- of a composite type (record/map/array) is not traversed down
-- if it has nil or box.NULL value.
--
-- The `:filter()` function returns a luafun iterator by all `w`
-- values accepted by the `f` function.
--
-- A composite node that is not accepted still traversed down.
--
-- Examples:
--
-- -- Do something for each piece of data that is marked by the
-- -- given annotation.
-- s:filter(function(w)
--     return w.schema.my_annotation ~= nil
-- end):each(function(w)
--     do_something(w.data)
-- end)
--
-- -- Group data by a value of an annotation.
-- local group_by_my_annotation = s:filter(function(w)
--     return w.schema.my_annotation ~= nil
-- end):map(function(w)
--     return w.schema.my_annotation, w.data
-- end):tomap()
--
-- Nuances:
--
-- * box.NULL is assumed as an existing value, so the
--   user-provided filter function `f` is called for it. However,
--   it is not called for `nil` values. See details below.
-- * While it is technically possible to pass information about
--   a field name for record/map field values and about an item
--   index for an array item value, it is not implemented for
--   simplicity (and because it is not needed in config's code).
-- * `w.path` for a map key and a map value are the same. It
--   seems, we should introduce some syntax to point a key in a
--   map, but the config module doesn't need it, so it is not
--   implemented.
--
-- nil/box.NULL nuances explanation
-- --------------------------------
--
-- Let's assume that a record defines three scalar fields: 'foo',
-- 'bar' and 'baz'. Let's name a schema object that wraps the
-- record as `s`.
--
-- * `s:filter(nil, f)` calls `f` only for the record itself.
-- * `s:filter(box.NULL, f` works in the same way.
-- * `s:filter({foo = box.NULL, bar = nil}, f)` calls `f` two
--   times: for the record and for the 'foo' field.
--
-- This behavior is needed to provide ability to handle box.NULL
-- values in the data somehow. It reflects the pairs() behavior on
-- a usual table, so it looks quite natural.
function methods.filter(self, data, f)
    local ctx = walkthrough_start(self, {acc = {}})
    filter_impl(rawget(self, 'schema'), data, f, ctx)
    return fun.iter(ctx.acc)
end

-- }}} <schema object>:filter()

-- {{{ <schema object>:map()

local function map_impl(schema, data, f, ctx)
    if is_scalar(schema) then
        -- We're reached a scalar: let's call the user-provided
        -- transformation function.
        local w = {
            schema = schema,
            path = table.copy(ctx.path),
            error = walkthrough_error_capture(ctx),
        }
        return f(data, w, ctx.f_ctx)
    elseif schema.type == 'record' then
        -- Traverse record's fields unconditionally: if the record
        -- itself is nil/box.NULL, if the fields are nil/box.NULL.
        --
        -- Missed values may be transformed to something non-nil.
        if data ~= nil then
            walkthrough_assert_table(ctx, schema, data)
        end

        -- Collect the new field values.
        local res = {}
        for field_name, field_def in pairs(schema.fields) do
            walkthrough_enter(ctx, field_name)

            local field
            if data ~= nil then
                field = data[field_name]
            end
            res[field_name] = map_impl(field_def, field, f, ctx)

            walkthrough_leave(ctx)
        end

        -- If the original value is nil/box.NULL and all the new
        -- fields are nil, let's preserve the original value as
        -- is: return nil/box.NULL instead of an empty table.
        if next(res) == nil and data == nil then
            return data -- nil or box.NULL
        end
        return res
    elseif schema.type == 'map' then
        -- If a map is nil/box.NULL, there is nothing to traverse.
        --
        -- Let's just return the original value.
        if data == nil then
            return data -- nil or box.NULL
        end

        walkthrough_assert_table(ctx, schema, data)

        local res = {}
        for field_name, field_value in pairs(data) do
            walkthrough_enter(ctx, field_name)
            local new_field_name = map_impl(schema.key, field_name, f, ctx)
            local new_field_value = map_impl(schema.value, field_value, f, ctx)
            res[new_field_name] = new_field_value
            walkthrough_leave(ctx)
        end
        return res
    elseif schema.type == 'array' then
        -- If an array is nil/box.NULL, there is nothing to
        -- traverse.
        --
        -- Just return the original value.
        if data == nil then
            return data -- nil or box.NULL
        end

        walkthrough_assert_table(ctx, schema, data)

        local res = {}
        for i, v in ipairs(data) do
            walkthrough_enter(ctx, i)
            local new_item_value = map_impl(schema.items, v, f, ctx)
            res[i] = new_item_value
            walkthrough_leave(ctx)
        end
        return res
    else
        assert(false)
    end
end

-- Transform data by the given function.
--
-- Leave the shape of the data unchanged.
--
-- Important: the data is assumed as already validated against
-- the given schema. (A fast type check is performed on composite
-- types, but it is not recommended to lean on it.)
--
-- The user-provided transformation function receives the
-- following three arguments in the given order:
--
-- * data -- value at the given path
-- * w -- walkthrough node, described below
-- * ctx -- user-provided context for the transformation function
--
-- The walkthrough node `w` has the following fields:
--
-- * w.schema -- schema node at the given path
-- * w.path -- path to the schema node
-- * w.error -- function that prepends a caller provided error
--   message with context information; use it for nice error
--   messages
--
-- An example of the mapping function:
--
-- local function f(data, w, ctx)
--     if w.schema.type == 'string' and data ~= nil then
--         return data:gsub('{{ *foo *}}', ctx.foo)
--     end
--     return data
-- end
--
-- The :map() method is recursive with certain rules:
--
-- * All record fields are traversed unconditionally, including
--   ones with nil/box.NULL values. Even if the record itself is
--   nil/box.NULL, its fields are traversed down (assuming their
--   values as nil).
--
--   It is important when the original data should be extended
--   using some information from the schema: say, default values.
-- * It is not the case for a map and an array: nil/box.NULL
--   fields and items are preserved as is, they're not traversed
--   down. If the map/the array itself is nil/box.NULL, it is
--   preserved as well.
--
--   A map has no list of fields in the schema, so it is not
--   possible to traverse it down. Similarly, an array has no
--   items count in the schema.
--
-- The method attempts to preserve the original shape of values
-- of a composite type:
--
-- * nil/box.NULL record is traversed down, but if all the new
--   field values are nil, the return value is the original one
--   (nil/box.NULL), not an empty table.
-- * nil/box.NULL values for a map and an array are preserved.
--
-- Nuances:
--
-- * The user-provided transformation function is called only for
--   scalars.
-- * nil/box.NULL handling for composite types. Described above.
-- * `w.path` for a map key and a map value are the same. It
--   seems, we should introduce some syntax to point a key in a
--   map, but the config module doesn't need it, so it is not
--   implemented.
function methods.map(self, data, f, f_ctx)
    local ctx = walkthrough_start(self, {f_ctx = f_ctx})
    return map_impl(rawget(self, 'schema'), data, f, ctx)
end

-- }}} <schema object>:map()

-- {{{ <schema object>:apply_default()

local function apply_default_f(data, w)
    -- The value is present, keep it.
    --
    -- box.NULL is assumed as a missed value.
    if data ~= nil then
        return data
    end

    -- Don't replace box.NULL in the original data with nil if
    -- there is no 'default' annotation.
    if type(w.schema.default) == 'nil' then
        return data
    end

    -- Determine whether to apply the default.
    --
    -- Perform the apply if the apply_default_if annotation
    -- returns true or if there is no such an annotation.
    --
    -- Keep the original value otherwise.
    local apply_default = true
    if w.schema.apply_default_if ~= nil then
        assert(type(w.schema.apply_default_if) == 'function')
        apply_default = w.schema.apply_default_if(data, w)
    end

    if apply_default then
        return w.schema.default
    end

    return data
end

-- Apply default values from the schema.
--
-- Important: the data is assumed as already validated against
-- the given schema. (A fast type check is performed on composite
-- types, but it is not recommended to lean on it.)
--
-- Nuances:
--
-- * Defaults are taken into account only for scalars.
--
-- Annotations taken into accounts:
--
-- * default -- the value to be placed instead of a missed one
-- * apply_default_if (function) -- whether to apply the default
--
--   apply_default_if = function(data, w)
--       -- w.schema -- current schema node
--       -- w.path -- path to the node
--       -- w.error -- for nice error messages
--   end
--
--   If there is no apply_default_if annotation, the default is
--   assumed as to be applied.
function methods.apply_default(self, data)
    return self:map(data, apply_default_f)
end

-- }}} <schema object>:apply_default()

-- {{{ <schema object>:merge()

local function merge_impl(schema, a, b, ctx)
    -- Prefer box.NULL over nil.
    if a == nil and b == nil then
        if type(a) == 'nil' then
            return b
        else
            return a
        end
    end

    -- Prefer X ~= nil over nil/box.NULL.
    if a == nil then
        return b
    elseif b == nil then
        return a
    end

    assert(a ~= nil and b ~= nil)

    -- Scalars and arrays are not to be merged.
    --
    -- At this point neither a == nil, nor b == nil, so return the
    -- preferred value, `b`.
    if is_scalar(schema) then
        return b
    elseif schema.type == 'array' then
        walkthrough_assert_table(ctx, schema, a)
        walkthrough_assert_table(ctx, schema, b)

        return b
    end

    -- `a` and `b` are both non-nil non-box.NULL records or maps.
    -- Perform the deep merge.
    if schema.type == 'record' then
        walkthrough_assert_table(ctx, schema, a)
        walkthrough_assert_table(ctx, schema, b)

        local res = {}
        for field_name, field_def in pairs(schema.fields) do
            walkthrough_enter(ctx, field_name)
            local a_field = a[field_name]
            local b_field = b[field_name]
            res[field_name] = merge_impl(field_def, a_field, b_field, ctx)
            walkthrough_leave(ctx)
        end
        return res
    elseif schema.type == 'map' then
        walkthrough_assert_table(ctx, schema, a)
        walkthrough_assert_table(ctx, schema, b)

        local res = {}
        for field_name, a_field in pairs(a) do
            walkthrough_enter(ctx, field_name)
            local b_field = b[field_name]
            res[field_name] = merge_impl(schema.value, a_field, b_field, ctx)
            walkthrough_leave(ctx)
        end
        -- NB: No error is possible, so let's skip
        -- walkthrough_enter()/walkthrough_leave().
        for field_name, b_field in pairs(b) do
            if type(a[field_name]) == 'nil' then
                res[field_name] = b_field
            end
        end
        return res
    else
        assert(false)
    end
end

-- Merge two hierarical values (prefer the latter).
--
-- Important: the data is assumed as already validated against
-- the given schema. (A fast type check is performed on composite
-- types, but it is not recommended to lean on it.)
--
-- box.NULL is preferred over nil, any X where X ~= nil is
-- preferred over nil/box.NULL.
--
-- Records and maps are deeply merged. Scalars and arrays are
-- all-or-nothing: the right hand one is chosen if both are
-- not nil/box.NULL.
--
-- The formal rules are below.
--
-- Let's define the merge result for nil and box.NULL values:
--
-- 1. merge(nil, nil) -> nil
-- 2. merge(nil, box.NULL) -> box.NULL
-- 3. merge(box.NULL, nil) -> box.NULL
-- 4. merge(box.NULL, box.NULL) -> box.NULL
--
-- Let's define X as a value that is not nil and is not box.NULL.
--
-- 5. merge(X, nil) -> X
-- 6. merge(X, box.NULL) -> X
-- 7. merge(nil, X) -> X
-- 8. merge(box.NULL, X) -> X
--
-- If the above conditions are not meet, the following type
-- specific rules are is effect.
--
-- 9. merge(<scalar A>, <scalar B>) -> <scalar B>
-- 10. merge(<array A>, <array B>) -> <array B>
-- 11. merge(<record A>, <record B>) -> deep-merge(A, B)
-- 12. merge(<map A>, <map B>) -> deep-merge(A, B)
--
-- For each key K in A and each key K in B: deep-merge(A, B)[K] is
-- merge(A[K], B[K]).
function methods.merge(self, a, b)
    local ctx = walkthrough_start(self)
    return merge_impl(rawget(self, 'schema'), a, b, ctx)
end

-- }}} <schema object>:merge()

-- {{{ <schema object>:pairs()

local function schema_pairs_append_node(schema, ctx)
    table.insert(ctx.acc, {
        path = table.copy(ctx.path),
        schema = schema,
    })
end

local function schema_pairs_impl(schema, ctx)
    if is_scalar(schema) then
        schema_pairs_append_node(schema, ctx)
    elseif schema.type == 'record' then
        for k, v in pairs(schema.fields) do
            walkthrough_enter(ctx, k)
            schema_pairs_impl(v, ctx)
            walkthrough_leave(ctx)
        end
    elseif schema.type == 'map' then
        schema_pairs_append_node(schema, ctx)
    elseif schema.type == 'array' then
        schema_pairs_append_node(schema, ctx)
    else
        assert(false)
    end
end

-- Walk over the schema and return scalar, array and map schema
-- nodes.
--
-- Usage example:
--
-- for _, w in schema:pairs() do
--     local path = w.path
--     local schema = w.schema
--     <...>
-- end
--
-- TODO: Rewrite it without collecting a list beforehand.
function methods.pairs(self)
    local ctx = walkthrough_start(self, {acc = {}})
    schema_pairs_impl(rawget(self, 'schema'), ctx)
    return fun.iter(ctx.acc)
end

-- }}} <schema object>:pairs()

-- {{{ Schema preprocessing

-- Step down to a schema node.
--
-- The function performs annotations tracking.
--
-- The annotations are tracked for all the nodes up to the root
-- forming a stack of annotations. It allows to step down to a
-- child node using preprocess_enter(), step up to the original
-- node using preprocess_leave() and step down again into another
-- child node.
--
-- At each given point the top annotation frame represents
-- annotations merged from the root node down to the given one.
-- If there are same named annotations on the path, then one from
-- a descendant node is preferred.
local function preprocess_enter(ctx, schema)
    assert(ctx.annotation_stack ~= nil)

    -- These keys are part of the schema node tree structure
    -- itself. See the 'Schema nodes constructors' section in this
    -- file for details about the schema node structure.
    local non_annotation_keys = {
        type = true,
        fields = true,
        key = true,
        value = true,
        items = true,
    }

    -- There are known annotations that barely has any sense in
    -- context of descendant schema nodes. Don't track them.
    local ignored_annotations = {
        allowed_values = true,
        validate = true,
        default = true,
        apply_default_if = true,
    }

    local frame = table.copy(ctx.annotation_stack[#ctx.annotation_stack] or {})
    for k, v in pairs(schema) do
        if not non_annotation_keys[k] and not ignored_annotations[k] then
            frame[k] = v
        end
    end
    table.insert(ctx.annotation_stack, frame)
end

-- Step up from a schema node.
--
-- Returns the computed fields for the node that we're leaving.
--
-- See preprocess_enter() for details.
local function preprocess_leave(ctx)
    return {
        annotations = table.remove(ctx.annotation_stack),
    }
end

-- The function prepares the given schema node tree in the
-- following way.
--
-- * The schema node is copied.
-- * All its descendant nodes are prepared using this algorithm
--   (recursively).
-- * Computed fields are calculated and stored in the copied node.
-- * The copied node is the result.
--
-- The sketchy structure of the returned schema node is the
-- following.
--
-- {
--     <..fields..>
--     computed = {
--         annotations = <...>,
--     },
-- }
--
-- At now there is only one computed field: `annotations`.
--
-- The `annotations` field contains all the annotations merged
-- from the root schema node down to the given one. If the same
-- annotation is present in an ancestor node and in an descendant
-- node, the latter is preferred.
--
-- Design details
-- --------------
--
-- The copying is performed to decouple schema node definitions
-- provided by a caller from the schema nodes stored in the schema
-- object. It allows to modify them without modifying caller's own
-- objects.
--
-- It is especially important if parts of one schema are passed
-- as schema node definitions to create another schema.
--
-- This case also motivates to store all the computed fields in
-- the `computed` field. This way it is easier to guarantee that
-- all the computed fields are stripped down from the original
-- schema nodes and don't affect the new schema anyhow.
local function preprocess_schema(schema, ctx)
    -- A schema node from another (already constructed) schema may
    -- be used in the schema that is currently under construction.
    --
    -- Since we're going to modify the schema node, we should copy
    -- it beforehand. Our modifications must not affect another
    -- schema.
    local res = table.copy(schema)

    -- Eliminate previously computed values if any.
    --
    -- The past values were computed against node's place in
    -- another schema, so it must not influence the node in its
    -- current place in the given schema.
    --
    -- This field is rewritten at end of the function, but it
    -- anyway should be stripped before going to
    -- preprocess_enter(). Otherwise it would be taken as an
    -- annotation.
    --
    -- (It can be just ignored in preprocess_enter(), but dropping
    -- it beforehand looks less error-prone.)
    res.computed = nil

    preprocess_enter(ctx, res)

    -- luacheck: ignore 542 empty if branch
    if is_scalar(schema) then
        -- Nothing to do.
    elseif schema.type == 'record' then
        local fields = {}
        for field_name, field_def in pairs(schema.fields) do
            fields[field_name] = preprocess_schema(field_def, ctx)
        end
        res.fields = fields
    elseif schema.type == 'map' then
        res.key = preprocess_schema(schema.key, ctx)
        res.value = preprocess_schema(schema.value, ctx)
    elseif schema.type == 'array' then
        res.items = preprocess_schema(schema.items, ctx)
    else
        assert(false)
    end

    res.computed = preprocess_leave(ctx)

    return res
end

-- }}} Schema preprocessing

-- {{{ Schema object constructor: new

-- Define a field lookup function on a schema object.
--
-- `<schema object>.foo` performs the following:
--
-- * search for a user-provided method
-- * search for a method defined in this module
-- * if 'name', 'schema' or 'methods' -- return the given field
-- * otherwise return nil
function schema_mt.__index(self, key)
    local instance_methods = rawget(self, 'methods')
    if instance_methods[key] ~= nil then
        return instance_methods[key]
    end
    if methods[key] ~= nil then
        return methods[key]
    end
    return rawget(self, key)
end

-- Create a schema object.
--
-- Unlike a schema node it has a name, has methods defined in this
-- module and user-provided methods.
local function new(name, schema, opts)
    local opts = opts or {}
    local instance_methods = opts.methods or {}

    assert(type(name) == 'string')
    assert(type(schema) == 'table')

    local ctx = {
        annotation_stack = {},
    }
    local preprocessed_schema = preprocess_schema(schema, ctx)

    return setmetatable({
        name = name,
        schema = preprocessed_schema,
        methods = instance_methods,
    }, schema_mt)
end

-- }}} Schema object constructor: new

-- {{{ schema.fromenv()

-- Forward declaration.
local fromenv

-- Decode a map value from an environment variable data.
--
-- Accepts two formats:
--
-- 1. JSON format (if data starts from "{").
-- 2. Simple foo=bar,baz=fiz object format (otherwise).
--
-- The simple format is applicable for a map with string keys and
-- scalar values of any type except 'any'.
local function map_from_env(env_var_name, raw_value, schema)
    local can_have_simple_format = schema.key.type == 'string' and
        is_scalar(schema.value) and schema.value.type ~= 'any'

    local recommendation = can_have_simple_format and
        'Use either the simple "foo=bar,baz=fiz" object format or the JSON ' ..
        'object format (starts from "{").' or
        'Use the JSON object format (starts from "{").'

    -- JSON object -> try to decode.
    if raw_value:startswith('{') then
        local ok, res = pcall(json.decode, raw_value)
        if not ok then
            error(('Unable to decode JSON data in environment ' ..
                'variable %q: %s'):format(env_var_name, res), 0)
        end
        return res
    end

    -- JSON array -> error.
    if raw_value:startswith('[') then
        error(('A JSON array is provided for environment variable %q of ' ..
            'type map, an object is expected. %s'):format(env_var_name,
            recommendation), 0)
    end

    -- Check several prerequisites for the simple foo=bar,baz=fiz
    -- object format. If any check is not passed, suggest to use
    -- the JSON object format and describe why.

    -- Allow only string keys.
    if schema.key.type ~= 'string' then
        error(('Use the JSON object format for environment variable %q: the ' ..
            'keys are supposed to have a non-string type (%q) that is not ' ..
            'supported by the simple "foo=bar,baz=fiz" object format. ' ..
            'A JSON object value starts from "{".'):format(env_var_name,
            schema.key.type), 0)
    end

    -- Allow only scalar field values. Forbid composite ones.
    if not is_scalar(schema.value) then
        error(('Use the JSON object format for environment variable %q: the ' ..
            'field values are supposed to have a composite type (%q) that ' ..
            'is not supported by the simple "foo=bar,baz=fiz" object ' ..
            'format. A JSON object value starts from "{".'):format(env_var_name,
            schema.value.type), 0)
    end

    -- Forbid scalar field values of the 'any' type.
    if schema.value.type == 'any' then
        error(('Use the JSON object format for environment variable %q: the ' ..
            'field values are supposed to have an arbitrary type ("any") ' ..
            'that is not supported by the simple "foo=bar,baz=fiz" object ' ..
            'format. A JSON object value starts from "{".'):format(
            env_var_name), 0)
    end

    -- Assume the simple foo=bar,baz=fiz object format.
    local err_msg_prefix = ('Unable to decode data in environment variable ' ..
        '%q assuming the simple "foo=bar,baz=fiz" object format'):format(
        env_var_name)
    local res = {}
    for _, v in ipairs(raw_value:split(',')) do
        local eq = v:find('=')
        if eq == nil then
            error(('%s: no "=" is found in a key-value pair. %s'):format(
                err_msg_prefix, recommendation), 0)
        end
        local lhs = string.sub(v, 1, eq - 1)
        local rhs = string.sub(v, eq + 1)

        if lhs == '' then
            error(('%s: no value before "=" is found in a key-value pair. ' ..
                '%s'):format(err_msg_prefix, recommendation), 0)
        end
        local subname = ('%s.%s'):format(env_var_name, lhs)
        res[lhs] = fromenv(subname, rhs, schema.value)
    end
    return res
end

-- Decode an array from an environment variable data.
--
-- Accepts two formats:
--
-- 1. JSON format (if data starts from "[").
-- 2. Simple foo,bar,baz array format (otherwise).
--
-- The simple format is applicable for an array with scalar item
-- values of any type except 'any'.
local function array_from_env(env_var_name, raw_value, schema)
    local can_have_simple_format = is_scalar(schema.items) and
        schema.items.type ~= 'any'

    local recommendation = can_have_simple_format and
        'Use either the simple "foo,bar,baz" array format or the JSON ' ..
        'array format (starts from "[").' or
        'Use the JSON array format (starts from "[").'

    -- JSON array -> try to decode.
    if raw_value:startswith('[') then
        local ok, res = pcall(json.decode, raw_value)
        if not ok then
            error(('Unable to decode JSON data in environment ' ..
                'variable %q: %s'):format(env_var_name, res), 0)
        end
        return res
    end

    -- JSON object -> error.
    if raw_value:startswith('{') then
        error(('A JSON object is provided for environment variable %q of ' ..
            'type array, an array is expected. %s'):format(env_var_name,
            recommendation), 0)
    end

    -- Check several prerequisites for the simple foo,bar,baz
    -- array format. If any check is not passed, suggest to use
    -- the JSON array format and describe why.

    -- Allow only scalar item values. Forbid composite ones.
    if not is_scalar(schema.items) then
        error(('Use the JSON array format for environment variable %q: the ' ..
            'item values are supposed to have a composite type (%q) that ' ..
            'is not supported by the simple "foo,bar,baz" array format. ' ..
            'A JSON array value starts from "[".'):format(env_var_name,
            schema.items.type), 0)
    end

    -- Forbid scalar field values of the 'any' type.
    if schema.items.type == 'any' then
        error(('Use the JSON array format for environment variable %q: the ' ..
            'item values are supposed to have an arbitrary type ("any") ' ..
            'that is not supported by the simple "foo,bar,baz" array ' ..
            'format. A JSON array value starts from "[".'):format(
            env_var_name), 0)
    end

    -- Assume the simple foo,bar,baz array format.
    local res = {}
    for i, v in ipairs(raw_value:split(',')) do
        local subname = ('%s[%d]'):format(env_var_name, i)
        res[i] = fromenv(subname, v, schema.items)
    end
    return res
end

-- Parse data from an environment variable as a value of the given
-- type.
--
-- Important: the result is not necessarily valid against the given
-- schema node. It should be validated using the
-- <schema object>:validate() method before further processing.
fromenv = function(env_var_name, raw_value, schema)
    if raw_value == nil or raw_value == '' then
        return nil
    end

    if is_scalar(schema) then
        local scalar_def = scalars[schema.type]
        assert(scalar_def ~= nil)
        return scalar_def.fromenv(env_var_name, raw_value)
    elseif schema.type == 'record' then
        -- TODO: It is technically possible to implement parsing
        -- of records similarly how it is done for maps, but it is
        -- not needed for the config module and left unimplemented
        -- for now.
        error(('Attempt to parse environment variable %q as a ' ..
            'record value: this is not supported yet and likely ' ..
            'caused by an internal error in the config module'):format(
            env_var_name), 0)
    elseif schema.type == 'map' then
        return map_from_env(env_var_name, raw_value, schema)
    elseif schema.type == 'array' then
        return array_from_env(env_var_name, raw_value, schema)
    else
        assert(false)
    end
end

-- }}} schema.fromenv()

return {
    -- Schema node constructors.
    scalar = scalar,
    record = record,
    map = map,
    array = array,

    -- Constructors for 'derived types'.
    --
    -- It produces a scalar, record, map or array, but annotates
    -- it in some specific way to, say, impose extra constraint
    -- rules at validation.
    enum = enum,
    set = set,

    -- Schema object constructor.
    new = new,

    -- Parse data from an environment variable.
    fromenv = fromenv,
}
