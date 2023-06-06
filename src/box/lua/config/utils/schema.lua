-- Schema-aware data manipulations.
--
-- The following annotations have some meaning for the module
-- itself (affects work of the schema methods):
--
-- * allowed_values
-- * validate
--
-- Others are just stored.

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
-- }

-- Verify whether the given value (data) has expected type and
-- produce a human readable error message otherwise.
local function validate_type_noexc(data, exp_type)
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
}

scalars.number = {
    type = 'number',
    validate_noexc = function(data)
        -- TODO: Should we accept cdata<int64_t> and
        -- cdata<uint64_t> here?
        return validate_type_noexc(data, 'number')
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
}

scalars.boolean = {
    type = 'boolean',
    validate_noexc = function(data)
        return validate_type_noexc(data, 'boolean')
    end,
}

scalars.any = {
    type = 'any',
    validate_noexc = function(_data)
        -- No validation.
        return true
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

    return setmetatable({
        name = name,
        schema = schema,
        methods = instance_methods,
    }, schema_mt)
end

-- }}} Schema object constructor: new

return {
    -- Schema node constructors.
    scalar = scalar,
    record = record,
    map = map,
    array = array,

    -- Schema object constructor.
    new = new,
}
