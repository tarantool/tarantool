local schema = require('internal.config.utils.schema')
local uuid = require('uuid')

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

local function validate_uuid_str(data, w)
    if uuid.fromstr(data) == nil then
        w.error('Unable to parse the value as a UUID: %q', data)
    end
    if data == uuid.NULL:str() then
        w.error('nil UUID is reserved')
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
    process = schema.record({
        strip_core = schema.scalar({
            type = 'boolean',
            box_cfg = 'strip_core',
            box_cfg_nondynamic = true,
            default = true,
        }),
        coredump = schema.scalar({
            type = 'boolean',
            box_cfg = 'coredump',
            box_cfg_nondynamic = true,
            default = false,
        }),
        background = schema.scalar({
            type = 'boolean',
            box_cfg = 'background',
            box_cfg_nondynamic = true,
            default = false,
        }),
        title = schema.scalar({
            type = 'string',
            box_cfg = 'custom_proc_title',
            default = 'tarantool - {{ instance_name }}',
        }),
        username = schema.scalar({
            type = 'string',
            box_cfg = 'username',
            box_cfg_nondynamic = true,
            default = box.NULL,
        }),
        work_dir = schema.scalar({
            type = 'string',
            box_cfg = 'work_dir',
            box_cfg_nondynamic = true,
            default = box.NULL,
        }),
        pid_file = schema.scalar({
            type = 'string',
            box_cfg = 'pid_file',
            box_cfg_nondynamic = true,
            mk_parent_dir = true,
            default = '{{ instance_name }}.pid',
        }),
    }),
    console = schema.record({
        enabled = schema.scalar({
            type = 'boolean',
            default = true,
        }),
        socket = schema.scalar({
            type = 'string',
            default = '{{ instance_name }}.control',
        }),
    }),
    fiber = schema.record({
        io_collect_interval = schema.scalar({
            type = 'number',
            box_cfg = 'io_collect_interval',
            default = box.NULL,
        }),
        too_long_threshold = schema.scalar({
            type = 'number',
            box_cfg = 'too_long_threshold',
            default = 0.5,
        }),
        worker_pool_threads = schema.scalar({
            type = 'number',
            box_cfg = 'worker_pool_threads',
            default = 4,
        }),
        slice = schema.record({
            warn = schema.scalar({
                type = 'number',
                default = 0.5,
            }),
            err = schema.scalar({
                type = 'number',
                default = 1,
            }),
        }),
        top = schema.record({
            enabled = schema.scalar({
                type = 'boolean',
                default = false,
            }),
        }),
    }),
    log = schema.record({
        -- The logger destination is handled separately in the
        -- box_cfg applier, so there are no explicit box_cfg and
        -- box_cfg_nondynamic annotations.
        --
        -- The reason is that there is no direct-no-transform
        -- mapping from, say, `log.file` to `box_cfg.log`.
        -- The applier should add the `file:` prefix.
        to = schema.enum({
            'stderr',
            'file',
            'pipe',
            'syslog',
        }, {
            default = 'stderr',
        }),
        file = schema.scalar({
            type = 'string',
            default = '{{ instance_name }}.log',
        }),
        pipe = schema.scalar({
            type = 'string',
            default = box.NULL,
        }),
        syslog = schema.record({
            identity = schema.scalar({
                type = 'string',
                default = 'tarantool',
            }),
            facility = schema.scalar({
                type = 'string',
                default = 'local7',
            }),
            server = schema.scalar({
                type = 'string',
                -- The logger tries /dev/log and then
                -- /var/run/syslog if no server is provided.
                default = box.NULL,
            }),
        }),
        nonblock = schema.scalar({
            type = 'boolean',
            box_cfg = 'log_nonblock',
            box_cfg_nondynamic = true,
            default = false,
        }),
        level = schema.scalar({
            type = 'number, string',
            box_cfg = 'log_level',
            default = 5,
            allowed_values = {
                0, 'fatal',
                1, 'syserror',
                2, 'error',
                3, 'crit',
                4, 'warn',
                5, 'info',
                6, 'verbose',
                7, 'debug',
            },
        }),
        format = schema.enum({
            'plain',
            'json',
        }, {
            box_cfg = 'log_format',
            default = 'plain',
        }),
        -- box.cfg({log_modules = <...>}) replaces the previous
        -- value without any merging.
        --
        -- If a key in this map is removed in the provided
        -- configuration, then it will be removed in the actually
        -- applied configuration.
        --
        -- It is exactly what we need there to make the
        -- configuration independent of previously applied values.
        modules = schema.map({
            key = schema.scalar({
                type = 'string',
            }),
            value = schema.scalar({
                type = 'number, string',
            }),
            box_cfg = 'log_modules',
            -- TODO: This default doesn't work now. It needs
            -- support of non-scalar schema nodes in
            -- <schema object>:map().
            default = box.NULL,
        }),
    }, {
        validate = function(log, w)
            if log.to == 'pipe' and log.pipe == nil then
                w.error('The pipe logger is set by the log.to parameter but ' ..
                    'the command is not set (log.pipe parameter)')
            end
        end,
    }),
    iproto = schema.record({
        -- XXX: listen/advertise are specific: accept a string of
        -- a particular format, a number (port), a table of a
        -- particular format.
        --
        -- Only a string (without further validation) is accepted
        -- for now.
        listen = schema.scalar({
            type = 'string',
            box_cfg = 'listen',
            default = box.NULL,
        }),
        advertise = schema.scalar({
            type = 'string',
            default = box.NULL,
        }),
        threads = schema.scalar({
            type = 'integer',
            box_cfg = 'iproto_threads',
            box_cfg_nondynamic = true,
            default = 1,
        }),
        net_msg_max = schema.scalar({
            type = 'integer',
            box_cfg = 'net_msg_max',
            default = 768,
        }),
        readahead = schema.scalar({
            type = 'integer',
            box_cfg = 'readahead',
            default = 16320,
        }),
    }),
    database = schema.record({
        instance_uuid = schema.scalar({
            type = 'string',
            box_cfg = 'instance_uuid',
            default = box.NULL,
            validate = validate_uuid_str,
        }),
        replicaset_uuid = schema.scalar({
            type = 'string',
            box_cfg = 'replicaset_uuid',
            default = box.NULL,
            validate = validate_uuid_str,
        }),
        hot_standby = schema.scalar({
            type = 'boolean',
            box_cfg = 'hot_standby',
            box_cfg_nondynamic = true,
            default = false,
        }),
        -- Reversed and applied to box_cfg.read_only.
        rw = schema.scalar({
            type = 'boolean',
            default = false,
        }),
        txn_timeout = schema.scalar({
            type = 'number',
            box_cfg = 'txn_timeout',
            default = 365 * 100 * 86400,
        }),
        txn_isolation = schema.enum({
            'read-committed',
            'read-confirmed',
            'best-effort',
        }, {
            box_cfg = 'txn_isolation',
            default = 'best-effort',
        }),
        use_mvcc_engine = schema.scalar({
            type = 'boolean',
            box_cfg = 'memtx_use_mvcc_engine',
            box_cfg_nondynamic = true,
            default = false,
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
