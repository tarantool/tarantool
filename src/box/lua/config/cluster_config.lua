local schema = require('internal.config.utils.schema')
local instance_config = require('internal.config.instance_config')
local expression = require('internal.config.utils.expression')

-- Extract a field from a table.
--
-- f({a = 1, b = 2}, 'a') -> {b = 2}, 1
--
-- The original table is not modified, but copied if necessary.
local function table_extract_field(t, field_name)
    local field_value = t[field_name]
    if field_value == nil then
        return t
    end
    t = table.copy(t)
    t[field_name] = nil
    return t, field_value
end

-- Cluster config methods.
local methods = {}

function methods.find_instance(_self, data, instance_name)
    -- Find group, replicaset, instance configuration for the
    -- given instance.
    local groups = data.groups or {}
    for gn, g in pairs(groups) do
        local replicasets = g.replicasets or {}
        for rn, r in pairs(replicasets) do
            local instances = r.instances or {}
            if instances[instance_name] ~= nil then
                return {
                    group = g,
                    replicaset = r,
                    instance = instances[instance_name],
                    group_name = gn,
                    replicaset_name = rn,
                }
            end
        end
    end

    return nil
end

function methods.instantiate(self, data, instance_name)
    -- No topology information provided.
    if data.groups == nil then
        return data
    end

    local found = self:find_instance(data, instance_name)

    if found == nil then
        local res = table.copy(data)
        res.groups = nil
        return res
    end

    local res = {}
    res = instance_config:merge(res, data)
    res = instance_config:merge(res, found.group)
    res = instance_config:merge(res, found.replicaset)
    res = instance_config:merge(res, found.instance)
    return res
end

-- Construct a record from other records and extra fields.
--
-- record_from_fields({
--     <record foo>
--     <record bar>
--     extra_field_baz = <...>,
--     extra_field_fiz = <...>,
-- })
--
-- It allows to write the cluster config schema in a more readable
-- way.
local function record_from_fields(fields)
    local res = {
        type = 'record',
        fields = {},
        -- <..annotations..>
    }

    for k, v in pairs(fields) do
        if type(k) == 'number' then
            -- Assume that a numeric key contains a record to
            -- copy its fields and annotations into the resulting
            -- record.
            assert(type(v) == 'table')
            assert(v.type == 'record')

            -- Copy fields.
            for kk, vv in pairs(v.fields) do
                assert(res.fields[kk] == nil, 'record_from_fields: duplicate '..
                                              'fields '..tostring(kk))
                res.fields[kk] = vv
            end

            -- Copy annotations.
            for kk, vv in pairs(v) do
                if kk ~= 'fields' and kk ~= 'type' then
                    assert(res[kk] == nil, 'record_from_fields: duplicate '..
                                           'annotations '..tostring(kk))
                    res[kk] = vv
                end
            end
        else
            -- Assume that a string key represents a field name
            -- and the corresponding value contains a schema node
            -- for the field.
            assert(type(k) == 'string')

            -- Copy the field.
            assert(res.fields[k] == nil, 'record_from_fields: duplicate '..
                                         'fields '..tostring(k))
            res.fields[k] = v
        end
    end

    return res
end

--
-- Return the instance configuration schema as a record with the given scope
-- annotation.
--
local function instance_config_with_scope(scope)
    assert(type(instance_config) == 'table')
    assert(type(instance_config.schema) == 'table')
    assert(instance_config.schema.type == 'record')

    local schema = table.copy(instance_config.schema)
    schema.scope = scope
    return schema
end

-- Validate instance_name, replicaset_name, group_name.
--
-- Please, keep it is-sync with src/box/node_name.[ch].
function methods.validate_name(_self, name)
    local NODE_NAME_LEN_MAX = 63
    if #name == 0 then
        return false, 'Zero length name is forbidden'
    end
    if #name > NODE_NAME_LEN_MAX then
        return false, ('A name must fit %d characters limit, got %q'):format(
            NODE_NAME_LEN_MAX, name)
    end
    if name:match('^[a-z0-9_-]+$') == nil then
        return false, ('A name must contain only lowercase letters, digits, ' ..
            'dash and underscore, got %q'):format(name)
    end
    if name:match('^[a-z]') == nil then
        return false, ('A name must start from a lowercase letter, ' ..
            'got %q'):format(name)
    end
    return true
end

-- A schema node that represents an instance name, a replicaset
-- name, a group name.
local name = schema.scalar({
    type = 'string',
    validate = function(name, w)
        local ok, err = methods.validate_name(nil, name)
        if not ok then
            w.error(err)
        end
    end,
})

-- Nested cluster config. It is an auxiliary schema.
--
-- The cluster config has the following structure.
--
-- {
--     conditional = array-of(*nested_cluster_config + if),
--     *nested_cluster_config,
-- }
--
-- Note 1: The asterisk denotes unpacking all the fields into the
-- parent schema node.
--
-- Note 2: The nested cluster config in the conditional section is
-- represented as an arbitrary map in the schema, but validatated
-- against the nested cluster config schema before merging into
-- the main config.
local schema_name = 'nested_cluster_config'
local nested_cluster_config = schema.new(schema_name, record_from_fields({
    instance_config_with_scope('global'),
    groups = schema.map({
        key = name,
        value = record_from_fields({
            instance_config_with_scope('group'),
            replicasets = schema.map({
                key = name,
                value = record_from_fields({
                    leader = schema.scalar({type = 'string'}),
                    bootstrap_leader = schema.scalar({type = 'string'}),
                    instance_config_with_scope('replicaset'),
                    instances = schema.map({
                        key = name,
                        value = instance_config_with_scope('instance'),
                    }),
                }),
            }),
        }),
    }),
}))

-- {{{ Support conditional sections

local conditional_vars = {
    tarantool_version = _TARANTOOL:match('^%d+%.%d+%.%d+'),
}
assert(conditional_vars.tarantool_version ~= nil)

-- Merge conditional sections into the main config.
--
-- Accepts a cluster configuration data with conditional sections
-- as an input and returns the data with the sections removed and
-- applied to the main config.
--
-- Conditional sections that don't fit the `if` clause criteria
-- are skipped.
--
--  | conditional:
--  | - aaa: {bbb: 1}
--  |   if: tarantool_version >= 3.0.0
--  | - ccc: {ddd: 2}
--  |   if: tarantool_version < 1.0.0
--  | eee: {fff: 3}
--
-- ->
--
--  | aaa: {bbb: 1}
--  | # no ccc
--  | eee: {fff: 3}
function methods.apply_conditional(_self, data)
    if data == nil then
        return data
    end
    local data, conditional = table_extract_field(data, 'conditional')
    if conditional == nil then
        return data
    end

    -- Look over the conditional sections and if the predicate in
    -- `if` field evaluates to `true`, merge the section in the
    -- data.
    local res = data
    for _, conditional_config in ipairs(conditional) do
        local config, expr = table_extract_field(conditional_config, 'if')
        if expression.eval(expr, conditional_vars) then
            -- NB: The nested configuration doesn't allow a
            -- presence of the 'conditional' field. Use the
            -- corresponding schema to perform the merge: nested
            -- cluster config.
            --
            -- It is also important that we use this schema
            -- instead of a map of 'any' values to perform a deep
            -- merge, not just replace fields at the top level.
            --
            -- NB: The section is already validated in
            -- validate_conditional().
            res = nested_cluster_config:merge(res, config)
        end
    end
    return res
end

-- Verify conditional sections, whose `if` predicate evaluates to
-- `true`.
--
-- Also, verify that the predicate is present in each of such
-- sections and has correct expression.
local function validate_conditional(data, w)
    -- Each conditional section should have 'if' key.
    local data, expr = table_extract_field(data, 'if')
    if expr == nil then
        w.error('A conditional section should have field "if"')
    end

    -- Fail the validation if the expression is incorrect.
    local ok, res = pcall(expression.eval, expr, conditional_vars)
    if not ok then
        w.error(res)
    end

    -- Fail the validation if this conditional section is to be
    -- applied, but it doesn't fit the schema.
    --
    -- NB: This validation doesn't accept 'conditional' field, so
    -- the appropriate schema is used for the validation (nested
    -- cluster config).
    if res then
        nested_cluster_config:validate(data)
    end
end

return schema.new('cluster_config', record_from_fields({
    conditional = schema.array({
        items = schema.map({
            -- This map represents a cluster config with
            -- additional 'if' key.
            --
            -- However, it is not necessarily represents a cluster
            -- config of the given tarantool version: it may be
            -- older or newer and so it may contain fields that
            -- are unknown from our point of view.
            --
            -- It is why the mapping type is used here: we allow
            -- arbitrary data and valitate only those items that
            -- are going to be merged into the main config
            -- (i.e. the validation is performed after the version
            -- comparison).
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'any'}),
            validate = validate_conditional,
        })
    }),
    nested_cluster_config.schema,
}), {
    methods = methods,
})

-- }}} Support conditional sections
