-- Schema-aware data manipulations.

local methods = {}
local schema_mt = {}

local scalars = {}

-- {{{ Scalar definitions

-- A scalar definition:
--
-- {
--     -- How the scalar is named.
--     type = <string>,
-- }

scalars.string = {
    type = 'string',
}

scalars.number = {
    type = 'number',
}

scalars.integer = {
    type = 'integer',
}

scalars.boolean = {
    type = 'boolean',
}

scalars.any = {
    type = 'any',
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
