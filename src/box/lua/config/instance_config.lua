local schema = require('internal.config.utils.schema')

-- List of annotations:
--
-- * enterprise_edition (boolean)
--
--   Available only in Tarantool Enterprise Edition.
--
-- * default (any)
--
--   Default value.
--
-- * scope ('global', 'group', 'replicaset', 'instance')
--
--   A place of an instance config option in the cluster config
--   hierarchy.
--
-- * box_cfg (string)
--
--   A name of the corresponding box.cfg() option.
--
-- * box_cfg_nondynamic (boolean)
--
--   `true` if the option can only be set at first box.cfg() call
--   and cannot be changed by a subsequent box.cfg() call.
--
-- * allowed_values
--
--   A list of allowed values.
--
-- * mkdir (boolean)
--
--   Create the given directory before box.cfg().
--
-- * mk_parent_dir (boolean)
--
--   Create a parent directory for the given file before box.cfg().

local CONFIG_VERSION = 'dev'

-- * In the instance config: must be present.
--
--   TODO: This check is disabled for the early config schema
--   version, because the schema is often changed and there is no
--   schema evolution support yet.
-- * In the cluster config: must be present in the global scope.
--
--   TODO: This check is disabled as well, see above.
-- * In the cluster config: must not be present in the other
--   scopes (group, replicaset, instance).
-- * If present, must correspond to one of supported config
--   versions, but is it validated by the config.version schema
--   node definition.
local function validate_config_version(data, w)
    -- scope == nil means that it is the instance config.
    local scope = w.schema.scope
    if scope == nil or scope == 'global' then
        -- Must be present.
        if data.config == nil or data.config.version == nil then
            -- TODO: Enable this check closer to 3.0.0 release.
            -- w.error('config.version is mandatory')
            return
        end
    else
        -- Must not be present.
        assert(scope == 'group' or scope == 'replicaset' or scope == 'instance')
        if data.config ~= nil and data.config.version ~= nil then
            w.error('config.version must not be present in the %s scope', scope)
        end
    end
end

return schema.new('instance_config', schema.record({
    config = schema.record({
        version = schema.enum({
            CONFIG_VERSION,
        }),
        reload = schema.enum({
            'auto',
            'manual',
        }, {
            default = 'auto',
        }),
    }),
}, {
    -- Any configuration data should contain a version of the
    -- config schema for which it is written.
    --
    -- This annotation cannot be placed right into the
    -- config.version field, because missed fields are not
    -- validated.
    validate = validate_config_version,
    -- Store the config schema version right in the outmost schema
    -- node as an annotation. It simplifies accesses from other
    -- code.
    config_version = CONFIG_VERSION,
}))
