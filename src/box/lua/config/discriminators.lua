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

return M
