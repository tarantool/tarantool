local utils = require('internal.utils')

local function find_coll(name)
    return box.space and box.space._collation.index.name:get{name}
end

local function find_func(name)
    return box.space and box.space._func.index.name:get(name)
end

local function find_space(name_or_id)
    return box.space and box.space[name_or_id]
end

-- Check and normalize constraint definition.
-- Given constraint @a constr is expected to be either a func name or
--  a table with function names and/or consraint name:function name pairs.
-- In case of error box.error.ILLEGAL_PARAMS is raised, and @a error_prefix
--  is added before string message.
local function normalize_constraint(constr, error_prefix, level)
    if type(constr) == 'string' then
        -- Short form of field constraint - just name of func,
        -- e.g.: {...constraint = "func_name"}
        local found = find_func(constr)
        if not found then
            box.error(box.error.ILLEGAL_PARAMS,
                      error_prefix .. "constraint function " ..
                      "was not found by name '" .. constr .. "'", level + 1)
        end
        -- normalize form of constraint.
        return {[constr] = found.id}
    elseif type(constr) == 'table' then
        -- Long form of field constraint - a table with:
        -- 1) func names 2) constraint name -> func name pairs.
        -- e.g.: {..., constraint = {func1, name2 = func2, ...}}
        local result = {}
        for constr_key, constr_func in pairs(constr) do
            if type(constr_func) ~= 'string' then
                box.error(box.error.ILLEGAL_PARAMS,
                          error_prefix .. "constraint function " ..
                          "is expected to be a string, " ..
                          "but got " .. type(constr_func), level + 1)
            end
            local found = find_func(constr_func)
            if not found then
                box.error(box.error.ILLEGAL_PARAMS,
                          error_prefix .. "constraint function " ..
                          "was not found by name '" .. constr_func .. "'",
                          level + 1)
            end
            local constr_name = nil
            if type(constr_key) == 'number' then
                -- 1) func name only.
                constr_name = constr_func
            elseif type(constr_key) == 'string' then
                -- 2) constraint name + func name pair.
                constr_name = constr_key
            else
                -- what are you?
                box.error(box.error.ILLEGAL_PARAMS,
                          error_prefix .. "constraint name " ..
                          "is expected to be a string, " ..
                          "but got " .. type(constr_key), level + 1)
            end
            -- normalize form of constraint pair.
            result[constr_name] = found.id
        end
        -- return normalized form of constraints.
        return result
    elseif constr then
        -- unrecognized form of constraint.
        box.error(box.error.ILLEGAL_PARAMS,
                  error_prefix .. "constraint must be string or table",
                  level + 1)
    end
    return nil
end

-- Helper of normalize_foreign_key.
-- Check and normalize one foreign key definition.
-- If not is_complex, field is expected to be a numeric ID or string name of
--  foreign field.
-- If is_complex, field is expected to be a table with local field ->
--  foreign field mapping.
-- If fkey_same_space, the foreign key refers to the same space.
local function normalize_foreign_key_one(def, error_prefix, is_complex,
                                         fkey_same_space, level)
    if def.field == nil then
        box.error(box.error.ILLEGAL_PARAMS,
                  error_prefix .. "foreign key: field must be specified",
                  level + 1)
    end
    if def.space ~= nil and
       type(def.space) ~= 'string' and type(def.space) ~= 'number' then
        box.error(box.error.ILLEGAL_PARAMS,
                  error_prefix .. "foreign key: space must be string or number",
                  level + 1)
    end
    local field = def.field
    if not is_complex then
        if type(field) ~= 'string' and type(field) ~= 'number' then
            box.error(box.error.ILLEGAL_PARAMS,
                      error_prefix ..
                      "foreign key: field must be string or number",
                      level + 1)
        end
        if type(field) == 'number' then
            -- convert to zero-based index.
            field = field - 1
        end
    else
        if type(field) ~= 'table' then
            box.error(box.error.ILLEGAL_PARAMS,
                      error_prefix .. "foreign key: field must be a table " ..
                      "with local field -> foreign field mapping", level + 1)
        end
        local count = 0
        local converted = {}
        for k,v in pairs(field) do
            count = count + 1
            if type(k) ~= 'string' and type(k) ~= 'number' then
                box.error(box.error.ILLEGAL_PARAMS,
                          error_prefix .. "foreign key: local field must be "
                          .. "string or number", level + 1)
            end
            if type(k) == 'number' then
                -- convert to zero-based index.
                k = k - 1
            end
            if type(v) ~= 'string' and type(v) ~= 'number' then
                box.error(box.error.ILLEGAL_PARAMS,
                          error_prefix .. "foreign key: foreign field must be "
                          .. "string or number", level + 1)
            end
            if type(v) == 'number' then
                -- convert to zero-based index.
                v = v - 1
            end
            converted[k] = v
        end
        if count < 1 then
            box.error(box.error.ILLEGAL_PARAMS,
                      error_prefix .. "foreign key: field must be a table " ..
                      "with local field -> foreign field mapping", level + 1)
        end
        field = utils.setmap(converted)
    end
    if not find_space(def.space) and not fkey_same_space then
        box.error(box.error.ILLEGAL_PARAMS,
                  error_prefix .. "foreign key: space " .. tostring(def.space)
                  .. " was not found", level + 1)
    end
    for k in pairs(def) do
        if k ~= 'space' and k ~= 'field' then
            box.error(box.error.ILLEGAL_PARAMS, error_prefix ..
                      "foreign key: unexpected parameter '" ..
                      tostring(k) .. "'", level + 1)
        end
    end
    if fkey_same_space then
        return {field = field}
    else
        return {space = find_space(def.space).id, field = field}
    end
end

-- Check and normalize foreign key definition.
-- Given definition @a fkey is expected to be one of:
-- {space=.., field=..}
-- {fkey_name={space=.., field=..}, }
-- If not is_complex, field is expected to be a numeric ID or string name of
--  foreign field.
-- If is_complex, field is expected to be a table with local field ->
--  foreign field mapping.
-- @a space_id and @a space_name - ID and name of a space, which contains the
--  foreign key.
-- In case of error box.error.ILLEGAL_PARAMS is raised, and @a error_prefix
--  is added before string message.
local function normalize_foreign_key(space_id, space_name, fkey, error_prefix,
                                     is_complex, level)
    if fkey == nil then
        return nil
    end
    if type(fkey) ~= 'table' then
        -- unrecognized form
        box.error(box.error.ILLEGAL_PARAMS,
                  error_prefix .. "foreign key must be a table", level + 1)
    end
    if fkey.field ~= nil and
        (type(fkey.space) ~= 'table' or type(fkey.field) ~= 'table') then
        -- the first, short form.
        local fkey_same_space = (fkey.space == nil or
                                 fkey.space == space_id or
                                 fkey.space == space_name)
        fkey = normalize_foreign_key_one(fkey, error_prefix, is_complex,
                                         fkey_same_space, level)
        local fkey_name = fkey_same_space and (space_name or 'unknown') or
                          find_space(fkey.space).name
        return {[fkey_name] = fkey}
    end
    -- the second, detailed form.
    local result = {}
    for k,v in pairs(fkey) do
        if type(k) ~= 'string' then
            box.error(box.error.ILLEGAL_PARAMS,
                      error_prefix .. "foreign key name must be a string",
                      level + 1)
        end
        if type(v) ~= 'table' then
            -- unrecognized form
            box.error(box.error.ILLEGAL_PARAMS,
                      error_prefix .. "foreign key definition must be a table "
                      .. "with 'space' and 'field' members", level + 1)
        end
        local fkey_same_space = (v.space == nil or
                                 v.space == space_id or
                                 v.space == space_name)
        v = normalize_foreign_key_one(v, error_prefix, is_complex,
                                      fkey_same_space, level)
        result[k] = v
    end
    return result
end

-- Check and normalize field default function.
local function normalize_default_func(func_name, error_prefix, level)
    if type(func_name) ~= 'string' then
        box.error(box.error.ILLEGAL_PARAMS,
                  error_prefix .. "field default function name is expected " ..
                  "to be a string, but got " .. type(func_name), level + 1)
    end
    local found = find_func(func_name)
    if not found then
        box.error(box.error.ILLEGAL_PARAMS,
                  error_prefix .. "field default function was not found by " ..
                  "name '" .. func_name .. "'", level + 1)
    end
    return found.id
end

local function normalize_format(space_id, space_name, format, level)
    local result = {}
    for i, given in ipairs(format) do
        local field = {}
        if type(given) ~= "table" then
            field.name = given
        else
            for k, v in pairs(given) do
                if k == 1 then
                    if given.name then
                        if not given.type then
                            field.type = v
                        else
                            field[1] = v
                        end
                    else
                        field.name = v
                    end
                elseif k == 2 and not given.type and not given.name then
                    field.type = v
                elseif k == 'collation' then
                    local coll = find_coll(v)
                    if not coll then
                        box.error(box.error.ILLEGAL_PARAMS,
                            "format[" .. i .. "]: collation " ..
                            "was not found by name '" .. v .. "'",
                            level + 1)
                    end
                    field[k] = coll.id
                elseif k == 'constraint' then
                    field[k] = normalize_constraint(v, "format[" .. i .. "]: ",
                                                    level + 1)
                elseif k == 'foreign_key' then
                    field[k] = normalize_foreign_key(space_id, space_name,
                                                     v, "format[" .. i .. "]: ",
                                                     false,
                                                     level + 1)
                elseif k == 'default_func' then
                    field[k] = normalize_default_func(v,
                                                      "format[" .. i .. "]: ",
                                                      level + 1)
                elseif k == 'compression' and type(given[k]) == 'table' then
                    field[k] = utils.setmap(given[k])
                else
                    field[k] = v
                end
            end
        end
        if type(field.name) ~= 'string' then
            box.error(box.error.ILLEGAL_PARAMS,
                      "format[" .. i .. "]: name (string) is expected",
                      level + 1)
        end
        if field.type == nil then
            field.type = 'any'
        elseif type(field.type) ~= 'string' then
            box.error(box.error.ILLEGAL_PARAMS,
                      "format[" .. i .. "]: type must be a string",
                      level + 1)
        end
        table.insert(result, field)
    end
    return result
end

local function denormalize_foreign_key_one(fkey)
    assert(type(fkey.field) == 'string' or type(fkey.field) == 'number')
    local result = fkey
    if type(fkey.field) == 'number' then
        -- convert to one-based index
        result.field = result.field + 1
    end
    return result
end

local function denormalize_foreign_key(fkey)
    local result = utils.setmap{}
    for k, v in pairs(fkey) do
        result[k] = denormalize_foreign_key_one(v)
    end
    return result
end

-- Convert zero-based foreign key field numbers to one-based
local function denormalize_format(format)
    local result = setmetatable({}, { __serialize = 'seq' })
    for i, f in ipairs(format) do
        result[i] = f
        for k, v in pairs(f) do
            if k == 'foreign_key' then
                result[i][k] = denormalize_foreign_key(v)
            end
        end
    end
    return result
end

box.internal.tuple_format.normalize_constraint = normalize_constraint
box.internal.tuple_format.normalize_foreign_key = normalize_foreign_key
box.internal.tuple_format.normalize_format = normalize_format
box.internal.tuple_format.denormalize_format = denormalize_format

-- new() needs a wrapper in Lua, because format normalization needs to be done
-- in Lua.
box.tuple.format.new = function(format)
    utils.check_param(format, 'format', 'table')
    format = normalize_format(nil, nil, format, 2)
    return box.internal.tuple_format.new(format)
end

setmetatable(box.tuple.format, {
    __call = function(_, t)
        return box.internal.tuple.tuple_get_format(t)
    end,
})
