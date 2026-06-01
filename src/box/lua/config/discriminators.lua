-- Store discriminator functions. Each key in this table corresponds
-- to a specific schema field path, and the associated value is a
-- function that selects a union variant.
local M = {}

local function table_is_array(data)
    assert(type(data) == 'table')

    local key_count = 0
    local min_key = 1/0  -- +inf
    local max_key = -1/0 -- -inf
    for k, _ in pairs(data) do
        if type(k) ~= 'number' then
            return false
        end
        if k - math.floor(k) ~= 0 then
            return false
        end
        key_count = key_count + 1
        min_key = math.min(min_key, k)
        max_key = math.max(max_key, k)
    end

    if key_count == 0 then
        return true
    end

    return min_key == 1 and max_key == key_count
end

local function find_variant(schema, schema_type)
    for _, variant in ipairs(schema.variants) do
        if variant.type == schema_type then
            return variant
        end
    end
    return nil
end

M['audit_log.spaces'] = function(data, w)
    if type(data) ~= 'table' then
        return nil
    end
    if table_is_array(data) then
        return find_variant(w.schema, 'array')
    end
    return find_variant(w.schema, 'map')
end

local builtin_metrics = {
    all = true,
    network = true,
    operations = true,
    system = true,
    replicas = true,
    info = true,
    slab = true,
    runtime = true,
    memory = true,
    spaces = true,
    fibers = true,
    cpu = true,
    vinyl = true,
    memtx = true,
    luajit = true,
    clock = true,
    event_loop = true,
    cpu_extended = true,
    schema = true,
}

-- The `metrics.include/exclude` schemas intentionally contain exactly two
-- scalar variants:
--   - one enum-like scalar with allowed_values for built-in metric groups,
--   - one plain string scalar for custom metric selectors.
--
-- Treat any other shape as a schema definition bug and fail early during
-- development.
M['metrics.include'] = function(data, w)
    if type(data) ~= 'string' then
        return nil
    end

    local schema_enum, schema_string

    -- We cannot use `find_variant` here because both variants have the same
    -- schema type (`scalar`). They differ only by the `allowed_values`
    -- annotation, so we have to inspect the variants manually.
    for _, variant in ipairs(w.schema.variants) do
        assert(variant.type == 'string')
        if variant.allowed_values ~= nil then
            schema_enum = variant
        else
            schema_string = variant
        end
    end

    assert(schema_enum and schema_string)

    if builtin_metrics[data] then
        return schema_enum
    end

    return schema_string
end

M['metrics.exclude'] = M['metrics.include']

return M
