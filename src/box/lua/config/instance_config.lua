local schema = require('experimental.config.utils.schema')
local tarantool = require('tarantool')
local uuid = require('uuid')
local urilib = require('uri')
local fio = require('fio')
local file = require('internal.config.utils.file')
local log = require('internal.config.utils.log')

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

-- Verify that replication.failover option is not present in the
-- instance scope of the cluster config.
local function validate_replication_failover(data, w)
    -- scope == nil means that it is the instance config.
    local scope = w.schema.scope

    -- There is no much sense to set the failover option for a
    -- particular instance, not the whole replicaset. So, the
    -- option is forbidden for the instance scope of the cluster
    -- config.
    --
    -- However, it is allowed for the instance config to accept an
    -- instantiated cluster config as valid.
    if scope ~= 'instance' then
        return
    end

    if data.replication ~= nil and data.replication.failover ~= nil then
        w.error('replication.failover must not be present in the %s scope',
            scope)
    end
end

local function validate_outmost_record(data, w)
    validate_replication_failover(data, w)
end

local function enterprise_edition_validate(data, w)
    -- OK if we're on Tarantool EE.
    if tarantool.package == 'Tarantool Enterprise' then
        return
    end

    assert(tarantool.package == 'Tarantool')

    -- OK, if the value is nil or box.NULL.
    if data == nil then
        return
    end

    -- NB: Let's fail the validation for an empty table, because
    -- otherwise we will get a less descriptive error from a lower
    -- level API. For example, box.cfg({wal_ext = {}}) on Tarantool
    -- Community Edition says the following:
    --
    -- > Incorrect value for option 'wal_ext': unexpected option

    w.error('This configuration parameter is available only in Tarantool ' ..
        'Enterprise Edition')
end

local function enterprise_edition_apply_default_if(_data, _w)
    return tarantool.package == 'Tarantool Enterprise'
end

-- Generate a function that calls two given functions in a row.
--
-- If one of the arguments is nil, return the other one.
--
-- If both arguments are nil, returns nil.
local function chain2(f1, f2)
    if f1 == nil then
        return f2
    end
    if f2 == nil then
        return f1
    end
    return function(...)
        f1(...)
        f2(...)
    end
end

-- Available only in Tarantool Enterprise Edition.
local function enterprise_edition(schema_node)
    schema_node.enterprise_edition = true

    -- Perform a domain specific validation first and only then
    -- check, whether the option is to be used with Tarantool
    -- Enterprise Edition.
    --
    -- This order is consistent with data type validation, which
    -- is also performed before the EE check.
    schema_node.validate = chain2(
        schema_node.validate,
        enterprise_edition_validate)
    schema_node.apply_default_if = chain2(
        schema_node.apply_default_if,
        enterprise_edition_apply_default_if)

    return schema_node
end

local function validate_uuid_str(data, w)
    if uuid.fromstr(data) == nil then
        w.error('Unable to parse the value as a UUID: %q', data)
    end
    if data == uuid.NULL:str() then
        w.error('nil UUID is reserved')
    end
end

-- Accepts an uri object (one produced by urilib.parse()).
--
-- Performs several checks regarding ability to use the URI to
-- create a client socket. IOW, to call connect() on it.
--
-- The function returns `true` if the URI is OK to connect and
-- `false, err` otherwise.
--
-- If the URI doesn't fit the given criteria an error is raised.
-- The function returns nothing otherwise.
--
-- The following checks are performed:
--
-- * INADDR_ANY IPv4 address (0.0.0.0) or in6addr_any IPv6 address
--   (::) in the host part of the URI.
--
--   It means 'bind to all interfaces' for the bind() call, but it
--   has no meaning at the connect() call on a client.
-- * Zero TCP port (service part of the URI).
--
--   It means 'bind to a random free port' for the bind() call,
--   but it has no meaning at the connect() call on a client.
local function uri_is_suitable_to_connect(uri)
    assert(uri ~= nil)

    if uri.ipv4 == '0.0.0.0' then
        return false, 'INADDR_ANY (0.0.0.0) cannot be used to create ' ..
            'a client socket'
    end
    if uri.ipv6 == '::' then
        return false, 'in6addr_any (::) cannot be used to create a client ' ..
            'socket'
    end
    if uri.service == '0' then
        return false, 'An URI with zero port cannot be used to create ' ..
            'a client socket'
    end

    return true
end

-- Verify 'uri' field in iproto.listen and iproto.advertise.* options.
local function validate_uri_field(has_login_field, used_to_connect)
    return function(data, w)
        -- Substitute variables with placeholders to don't confuse
        -- the URI parser with the curly brackets.
        data = data:gsub('{{ *.- *}}', 'placeholder')
        local uri, err = urilib.parse(data)
        if uri == nil then
            if data:find(',') then
                w.error('A single URI is expected, not a list of URIs')
            end
            w.error('Unable to parse an URI: %s', err)
        end
        if uri.login ~= nil and has_login_field then
            w.error("Login must be set via the 'login' option")
        end
        if uri.login ~= nil and not has_login_field then
            w.error('Login cannot be set for as part of the URI')
        end
        assert(uri.password == nil)
        if uri.params ~= nil then
            w.error("URI parameters should be described in the 'params' " ..
                "field, not as the part of URI")
        end
        if used_to_connect then
            local ok, err = uri_is_suitable_to_connect(uri)
            if not ok then
                w.error("bad URI %q: %s", data, err)
            end
        end
    end
end

-- Accept a value of 'iproto.listen' option and return the first URI
-- that is suitable to create a client socket (not just to listen
-- on the server as, say, 0.0.0.0:3301 or localhost:0).
--
-- See the uri_is_suitable_to_connect() method in the instance
-- schema object for details.
local function find_suitable_uri_to_connect(listen, opts)
    local opts = opts or {}
    assert(opts.log_prefix == nil or type(opts.log_prefix) == 'string')

    for _, u in ipairs(listen) do
        assert(u.uri ~= nil)
        local uri = urilib.parse(u.uri)
        if uri ~= nil then
            local ok, err = uri_is_suitable_to_connect(uri)
            if ok then
                -- The urilib.format() call has the second optional
                -- argument `write_password`. Let's assume that the
                -- given URIs are to listen on them and so have no
                -- user/password.
                --
                -- NB: We need to format the URI back to construct the
                -- 'uri' field. urilib.parse() creates separate 'host'
                -- and 'service'.
                return urilib.format(uri), u.params
            elseif opts.log_prefix then
                log.warn(("%sunsuitable URI %q: %s")
                                :format(opts.log_prefix, u.uri, err))
            end
        end
    end
    return nil
end

local function find_password(self, iconfig, username)
    -- The guest user can't have a password.
    if username == 'guest' then
        return nil
    end

    -- Find a user definition in the config.
    local user_def = self:get(iconfig, 'credentials.users.' .. username)
    if user_def == nil then
        error(('Cannot find user %s in the config to use its password in a '..
               'replication peer URI'):format(username), 0)
    end

    -- There is a user definition without a password. Let's assume
    -- that the user has no password.
    if user_def.password ~= nil then
        return user_def.password
    end
    return nil
end

local function instance_uri(self, iconfig, advertise_type, opts)
    assert(advertise_type == 'peer' or advertise_type == 'sharding')

    -- An effective value of iproto.advertise.sharding defaults to
    -- iproto.advertise.peer.
    local uri
    if advertise_type == 'sharding' then
        uri = self:get(iconfig, 'iproto.advertise.sharding')
        if uri == nil then
            advertise_type = 'peer'
        end
    end
    if advertise_type == 'peer' then
        uri = self:get(iconfig, 'iproto.advertise.peer')
    end
    -- If there is no uri field, look it in iproto.listen.
    uri = uri ~= nil and uri or {}
    if uri.uri == nil then
        assert(uri.params == nil)
        local listen = self:get(iconfig, 'iproto.listen')
        if listen == nil then
            return nil
        end
        uri = table.copy(uri)
        uri.uri, uri.params = find_suitable_uri_to_connect(listen, opts)
    end
    -- No URI found for the given instance.
    if uri.uri == nil then
        return nil
    end

    -- If there is a login. but there are no password: lookup the
    -- credentials section.
    if uri.password == nil and uri.login ~= nil then
        uri = table.copy(uri)
        uri.password = find_password(self, iconfig, uri.login)
    end
    return uri
end

local function apply_vars_f(data, w, vars)
    if w.schema.type == 'string' and data ~= nil then
        assert(type(data) == 'string')
        return (data:gsub('{{ *(.-) *}}', function(var_name)
            if vars[var_name] ~= nil then
                return vars[var_name]
            end
            w.error(('Unknown variable %q'):format(var_name))
        end))
    end
    return data
end

-- The file path is interpreted as relative to `process.work_dir`.
-- The first box.cfg() call sets a current working directory to this path.
--
-- However, we should prepend paths manually before the first
-- box.cfg() call.
--
-- This function returns the prefix to add into config's paths.
--
-- TODO: Glue all such code from applier/mkdir.lua and
-- utils/snapshot.lua.
local function base_dir(self, iconfig)
    local work_dir = self:get(iconfig, 'process.work_dir')
    return type(box.cfg) == 'function' and work_dir or nil
end

-- Interpret the given path as relative to the given base
-- directory.
local function rebase_file_path(base_dir, path)
    -- fio.pathjoin('/foo', '/bar') gives '/foo/bar/', see
    -- gh-8816.
    --
    -- Let's check whether the second path is absolute.
    local needs_prepending = base_dir ~= nil and not path:startswith('/')
    if needs_prepending then
        path = fio.pathjoin(base_dir, path)
    end

    -- Let's consider the following example.
    --
    -- config:
    --   context:
    --     foo:
    --       from: file
    --       file: ../foo.txt
    -- process:
    --   work_dir: x
    --
    -- In such a case we get correct "x/../foo.txt" on
    -- startup, but fio.open() complains if there is no "x"
    -- directory.
    --
    -- The working directory is created at startup if needed,
    -- so a user is not supposed to create it manually before
    -- first tarantool startup.
    --
    -- Let's strip all these ".." path components using
    -- fio.abspath().
    path = fio.abspath(path)

    -- However, if possible, use a relative path. Sometimes an
    -- absolute path overruns the Unix socket path length limit
    -- (107 on Linux and 103 on Mac OS -- in our case).
    --
    -- Using a relative path allows to listen on the given Unix
    -- socket path in such cases.
    local cwd = fio.cwd()
    if cwd == nil then
        return path
    end
    if not cwd:endswith('/') then
        cwd = cwd .. '/'
    end
    if path:startswith(cwd) then
        path = path:sub(#cwd + 1, #path)
    end
    return path
end

-- Interpret the given path as relative to the given working
-- directory from the config.
--
-- This function takes care to the chdir performed by the first
-- box.cfg() call if the working directory is set.
local function prepare_file_path(self, iconfig, path)
    local base_dir = self:base_dir(iconfig)
    return rebase_file_path(base_dir, path)
end

-- Read a config.context[name] variable depending on its "from"
-- type.
local function read_context_var_noexc(base_dir, def)
    if def.from == 'env' then
        local value = os.getenv(def.env)
        if value == nil then
            return false, ('no %q environment variable'):format(def.env)
        end
        return true, value
    elseif def.from == 'file' then
        local path = rebase_file_path(base_dir, def.file)
        return pcall(file.universal_read, path, 'file')
    else
        assert(false)
    end
end

local function apply_vars(self, iconfig, vars)
    vars = table.copy(vars)

    local base_dir = self:base_dir(iconfig)

    -- Read config.context.* variables and add them into the
    -- variables list.
    local context_vars = self:get(iconfig, 'config.context')
    for name, def in pairs(context_vars or {}) do
        local ok, res = read_context_var_noexc(base_dir, def)
        if not ok then
            error(('Unable to read config.context.%s variable value: ' ..
                '%s'):format(name, res), 0)
        end
        if def.rstrip then
            res = res:rstrip()
        end
        vars['context.' .. name] = res
    end

    return self:map(iconfig, apply_vars_f, vars)
end

local function feedback_apply_default_if(_data, _w)
    return box.internal.feedback_daemon ~= nil
end

local function feedback_validate(data, w)
    if data == nil or box.internal.feedback_daemon ~= nil then
        return
    end
    w.error('Tarantool is built without feedback reports sending support')
end

return schema.new('instance_config', schema.record({
    config = schema.record({
        reload = schema.enum({
            'auto',
            'manual',
        }, {
            default = 'auto',
        }),
        -- Defaults can't be set there, because the `validate`
        -- annotation expects either no data or data with existing
        -- prefix field. The prefix field has no default. So,
        -- applying defaults to an empty data would make the data
        -- invalid.
        etcd = enterprise_edition(schema.record({
            prefix = schema.scalar({
                type = 'string',
                validate = function(data, w)
                    if not data:startswith('/') then
                        w.error(('config.etcd.prefix should be a path alike ' ..
                            'value, got %q'):format(data))
                    end
                end,
            }),
            endpoints = schema.array({
                items = schema.scalar({
                    type = 'string',
                }),
            }),
            username = schema.scalar({
                type = 'string',
            }),
            password = schema.scalar({
                type = 'string',
            }),
            http = schema.record({
                request = schema.record({
                    timeout = schema.scalar({
                        type = 'number',
                        -- default = 0.3 is applied right in the
                        -- etcd source. See a comment above
                        -- regarding defaults in config.etcd.
                    }),
                    unix_socket = schema.scalar({
                        type = 'string',
                    }),
                }),
            }),
            watchers = schema.record({
                reconnect_timeout = schema.scalar({
                    type = 'number',
                    -- default = 1.0 is applied right in the
                    -- etcd source. See a comment above
                    -- regarding defaults in config.etcd.
                }),
                reconnect_max_attempts = schema.scalar({
                    type = 'integer',
                    -- default = 10 is applied right in the
                    -- etcd source. See a comment above
                    -- regarding defaults in config.etcd.
                }),
            }),
            ssl = schema.record({
                ssl_key = schema.scalar({
                    type = 'string',
                }),
                ssl_cert = schema.scalar({
                    type = 'string',
                }),
                ca_path = schema.scalar({
                    type = 'string',
                }),
                ca_file = schema.scalar({
                    type = 'string',
                }),
                verify_peer = schema.scalar({
                    type = 'boolean',
                }),
                verify_host = schema.scalar({
                    type = 'boolean',
                }),
            }),
        }, {
            validate = function(data, w)
                -- No config.etcd section at all -- OK.
                if data == nil or next(data) == nil then
                    return
                end
                -- There is some data -- the prefix should be there.
                if data.prefix == nil then
                    w.error('No config.etcd.prefix provided')
                end
            end,
        })),
        storage = enterprise_edition(schema.record({
            prefix = schema.scalar({
                type = 'string',
                validate = function(data, w)
                    if not data:startswith('/') then
                        w.error(('config.storage.prefix should be ' ..
                            'a path alike value, got %q'):format(data))
                    end
                end,
            }),
            endpoints = schema.array({
                items = schema.record({
                    uri = schema.scalar({
                        type = 'string',
                    }),
                    login = schema.scalar({
                        type = 'string',
                    }),
                    password = schema.scalar({
                        type = 'string',
                    }),
                    params = schema.record({
                        transport = schema.enum({
                            'plain',
                            'ssl',
                        }),
                        ssl_key_file = enterprise_edition(schema.scalar({
                            type = 'string',
                        })),
                        ssl_cert_file = enterprise_edition(schema.scalar({
                            type = 'string',
                        })),
                        ssl_ca_file = enterprise_edition(schema.scalar({
                            type = 'string',
                        })),
                        ssl_ciphers = enterprise_edition(schema.scalar({
                            type = 'string',
                        })),
                        ssl_password = enterprise_edition(schema.scalar({
                            type = 'string',
                        })),
                        ssl_password_file = enterprise_edition(schema.scalar({
                            type = 'string',
                        })),
                    }),
                }),
                validate = function(data, w)
                    if #data == 0 then
                        w.error('At least one endpoint must be' ..
                            'specified in config.storage.endpoints')
                    end
                end,
            }),
            timeout = schema.scalar({
                type = 'number',
                default = 3,
            }),
            reconnect_after = schema.scalar({
                type = 'number',
                default = 3,
            })
        }, {
            validate = function(data, w)
                if data == nil or next(data) == nil then
                    return
                end
                if data.prefix == nil and data.endpoints == nil then
                    return
                end
                if data.prefix == nil then
                    w.error('No config.storage.prefix provided')
                end
                if data.endpoints == nil then
                    w.error('No config.storage.endpoints provided')
                end
            end
        })),
        context = schema.map({
            key = schema.scalar({
                type = 'string',
            }),
            value = schema.record({
                from = schema.enum({
                    'env',
                    'file',
                }),
                env = schema.scalar({
                    type = 'string',
                }),
                file = schema.scalar({
                    type = 'string',
                }),
                rstrip = schema.scalar({
                    type = 'boolean',
                }, {
                    default = false,
                }),
            }, {
                validate = function(var, w)
                    if var.from == nil then
                        w.error('"from" field must be defined in a context ' ..
                            'variable definition')
                    end
                    if var.from == 'env' and var.env == nil then
                        w.error('"env" field must define an environment ' ..
                            'variable name if "from" field is set to "env"')
                    end
                    if var.from == 'file' and var.file == nil then
                        w.error('"file" field must define a file name if ' ..
                            '"from" field is set to "file"')
                    end
                end,
            }),
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
            -- The mkdir annotation is not present here, because
            -- otherwise the directory would be created
            -- unconditionally. Instead, mkdir applier creates it
            -- only before the first box.cfg() call.
            default = box.NULL,
        }),
        pid_file = schema.scalar({
            type = 'string',
            box_cfg = 'pid_file',
            box_cfg_nondynamic = true,
            mk_parent_dir = true,
            default = 'var/run/{{ instance_name }}/tarantool.pid',
        }),
    }),
    console = schema.record({
        enabled = schema.scalar({
            type = 'boolean',
            default = true,
        }),
        socket = schema.scalar({
            type = 'string',
            -- The mk_parent_dir annotation is not present here,
            -- because otherwise the directory would be created
            -- unconditionally. Instead, mkdir applier creates it
            -- if console.enabled is true.
            default = 'var/run/{{ instance_name }}/tarantool.control',
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
            -- The mk_parent_dir annotation is not present here,
            -- because otherwise the directory would be created
            -- unconditionally. Instead, mkdir applier creates it
            -- if log.to is 'file'.
            default = 'var/log/{{ instance_name }}/tarantool.log',
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
        listen = schema.array({
            items = schema.record({
                uri = schema.scalar({
                    type = 'string',
                    validate = validate_uri_field(false, false),
                }),
                params = schema.record({
                    transport = schema.enum({
                        'plain',
                        'ssl',
                    }),
                    -- Mandatory server options for TLS.
                    ssl_key_file = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                    ssl_cert_file = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                    -- Optional server options for TLS.
                    ssl_ca_file = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                    ssl_ciphers = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                    ssl_password = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                    ssl_password_file = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                }),
            }, {
                validate = function(data, w)
                    -- If data is not nil then the URI should be there.
                    if data.uri == nil then
                        w.error('The URI is required for iproto.listen')
                    end
                end,
            }),
            box_cfg = 'listen',
        }),
        -- URIs for clients to let them know where to connect.
        --
        -- There are several possibly different URIs:
        --
        -- * client
        --
        --   The informational value for clients. It is not used
        --   in tarantool anyhow (only validated). Contains only
        --   host:port: no user, no password.
        --
        --   Must be suitable to connect (no INADDR_ANY, no
        --   in6addr_any, no zero port).
        --
        --   Note: the host:port part may represent a Unix domain
        --   socket: host = 'unix/', port = '/path/to/socket'.
        --
        -- * peer
        --
        --   The general purpose peer URI, used for connections
        --   within the cluster (replica -> master, router ->
        --   storage, rebalancer -> storage).
        --
        --   Usually points to a user with the 'replication' role.
        --
        -- * sharding
        --
        --   The URI for router and rebalancer.
        --
        --   If unset, the general peer URI should be used.
        --
        -- The iproto.advertise.{peer,sharding} options have the
        -- following syntax variants:
        --
        -- With URI:
        -- {uri = ...}
        -- {uri = ..., params = ...}
        -- {login = ..., uri = ...}
        -- {login = ..., password = ..., uri = ...}
        -- {login = ..., uri = ..., params = ...}
        -- {login = ..., password = ..., uri = ..., params = ...}
        --
        -- Without URI:
        --
        -- {login = ...}
        -- {login = ..., password = ...}
        --
        -- where uri is host:port.
        --
        -- Note: the host:port part may represent a Unix domain
        -- socket: host = 'unix/', port = '/path/to/socket'.
        --
        -- If there is no uri field, it is to be looked in
        -- iproto.listen.
        --
        -- If there is a user, but no password, the password is to
        -- be looked in the `credentials` section of the
        -- configuration (except user 'guest', which can't  have a
        -- password).
        advertise = schema.record({
            client = schema.scalar({
                type = 'string',
                default = box.NULL,
                validate = validate_uri_field(false, true),
            }),
            peer = schema.record({
                login = schema.scalar({
                    type = 'string',
                }),
                password = schema.scalar({
                    type = 'string',
                }),
                uri = schema.scalar({
                    type = 'string',
                    validate = validate_uri_field(true, true),
                }),
                params = schema.record({
                    transport = schema.enum({
                        'plain',
                        'ssl',
                    }),
                    ssl_ca_file = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                    ssl_cert_file = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                    ssl_ciphers = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                    ssl_key_file = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                    ssl_password = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                    ssl_password_file = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                }),
            }, {
                validate = function(data, w)
                    if next(data) == nil then
                        w.error('An URI should have at least one field')
                    end
                    -- If a password is set, a login must also be specified.
                    if data.password ~= nil and data.login == nil then
                        w.error('Password cannot be set without setting login')
                    end
                    -- If a params is set, an uri must also be specified.
                    if data.params ~= nil and data.uri == nil then
                        w.error('Params cannot be set without setting uri')
                    end
                end,
            }),
            sharding = schema.record({
                login = schema.scalar({
                    type = 'string',
                }),
                password = schema.scalar({
                    type = 'string',
                }),
                uri = schema.scalar({
                    type = 'string',
                    validate = validate_uri_field(true, true),
                }),
                params = schema.record({
                    transport = schema.enum({
                        'plain',
                        'ssl',
                    }),
                    ssl_ca_file = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                    ssl_cert_file = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                    ssl_ciphers = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                    ssl_key_file = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                    ssl_password = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                    ssl_password_file = enterprise_edition(schema.scalar({
                        type = 'string',
                    })),
                }),
            }, {
                validate = function(data, w)
                    if next(data) == nil then
                        w.error('An URI should have at least one field')
                    end
                    -- If a password is set, a login must also be specified.
                    if data.password ~= nil and data.login == nil then
                        w.error('Password cannot be set without setting login')
                    end
                    -- If a params is set, an uri must also be specified.
                    if data.params ~= nil and data.uri == nil then
                        w.error('Params cannot be set without setting uri')
                    end
                end,
            }),
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
        -- Applied to box_cfg.read_only.
        --
        -- The effective default depends on amount of instances in
        -- a replicaset.
        --
        -- A singleton instance (the only instance in the
        -- replicaset) is in the 'rw' mode by default.
        --
        -- If the replicaset contains several (more than one)
        -- instances, the default is 'ro'.
        mode = schema.enum({
            'ro',
            'rw',
        }, {
            default = box.NULL,
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
    sql = schema.record({
        cache_size = schema.scalar({
            type = 'integer',
            box_cfg = 'sql_cache_size',
            default = 5 * 1024 * 1024,
        }),
    }),
    memtx = schema.record({
        memory = schema.scalar({
            type = 'integer',
            box_cfg = 'memtx_memory',
            default = 256 * 1024 * 1024,
        }),
        allocator = schema.enum({
            'small',
            'system',
        }, {
            box_cfg = 'memtx_allocator',
            box_cfg_nondynamic = true,
            default = 'small',
        }),
        slab_alloc_granularity = schema.scalar({
            type = 'integer',
            box_cfg = 'slab_alloc_granularity',
            box_cfg_nondynamic = true,
            default = 8,
        }),
        slab_alloc_factor = schema.scalar({
            type = 'number',
            box_cfg = 'slab_alloc_factor',
            box_cfg_nondynamic = true,
            default = 1.05,
        }),
        min_tuple_size = schema.scalar({
            type = 'integer',
            box_cfg = 'memtx_min_tuple_size',
            box_cfg_nondynamic = true,
            default = 16,
        }),
        max_tuple_size = schema.scalar({
            type = 'integer',
            box_cfg = 'memtx_max_tuple_size',
            default = 1024 * 1024,
        }),
        sort_threads = schema.scalar({
            type = 'integer',
            box_cfg = 'memtx_sort_threads',
            box_cfg_nondynamic = true,
            default = box.NULL,
        }),
    }),
    vinyl = schema.record({
        bloom_fpr = schema.scalar({
            type = 'number',
            box_cfg = 'vinyl_bloom_fpr',
            box_cfg_nondynamic = true,
            default = 0.05,
        }),
        cache = schema.scalar({
            type = 'integer',
            box_cfg = 'vinyl_cache',
            default = 128 * 1024 * 1024,
        }),
        defer_deletes = schema.scalar({
            type = 'boolean',
            box_cfg = 'vinyl_defer_deletes',
            default = false,
        }),
        dir = schema.scalar({
            type = 'string',
            box_cfg = 'vinyl_dir',
            box_cfg_nondynamic = true,
            mkdir = true,
            default = 'var/lib/{{ instance_name }}',
        }),
        max_tuple_size = schema.scalar({
            type = 'integer',
            box_cfg = 'vinyl_max_tuple_size',
            default = 1024 * 1024,
        }),
        memory = schema.scalar({
            type = 'integer',
            box_cfg = 'vinyl_memory',
            default = 128 * 1024 * 1024,
        }),
        page_size = schema.scalar({
            type = 'integer',
            box_cfg = 'vinyl_page_size',
            box_cfg_nondynamic = true,
            default = 8 * 1024,
        }),
        range_size = schema.scalar({
            type = 'integer',
            box_cfg = 'vinyl_range_size',
            box_cfg_nondynamic = true,
            default = box.NULL,
        }),
        read_threads = schema.scalar({
            type = 'integer',
            box_cfg = 'vinyl_read_threads',
            box_cfg_nondynamic = true,
            default = 1,
        }),
        run_count_per_level = schema.scalar({
            type = 'integer',
            box_cfg = 'vinyl_run_count_per_level',
            box_cfg_nondynamic = true,
            default = 2,
        }),
        run_size_ratio = schema.scalar({
            type = 'number',
            box_cfg = 'vinyl_run_size_ratio',
            box_cfg_nondynamic = true,
            default = 3.5,
        }),
        timeout = schema.scalar({
            type = 'number',
            box_cfg = 'vinyl_timeout',
            default = 60,
        }),
        write_threads = schema.scalar({
            type = 'integer',
            box_cfg = 'vinyl_write_threads',
            box_cfg_nondynamic = true,
            default = 4,
        }),
    }),
    wal = schema.record({
        dir = schema.scalar({
            type = 'string',
            box_cfg = 'wal_dir',
            box_cfg_nondynamic = true,
            mkdir = true,
            default = 'var/lib/{{ instance_name }}',
        }),
        mode = schema.enum({
            'none',
            'write',
            'fsync',
        }, {
            box_cfg = 'wal_mode',
            box_cfg_nondynamic = true,
            default = 'write',
        }),
        max_size = schema.scalar({
            type = 'integer',
            box_cfg = 'wal_max_size',
            box_cfg_nondynamic = true,
            default = 256 * 1024 * 1024,
        }),
        dir_rescan_delay = schema.scalar({
            type = 'number',
            box_cfg = 'wal_dir_rescan_delay',
            default = 2,
        }),
        queue_max_size = schema.scalar({
            type = 'integer',
            box_cfg = 'wal_queue_max_size',
            default = 16 * 1024 * 1024,
        }),
        cleanup_delay = schema.scalar({
            type = 'number',
            box_cfg = 'wal_cleanup_delay',
            -- No default value here - the option is deprecated
            -- and shouldn't be used by default.
        }),
        retention_period = enterprise_edition(schema.scalar({
            type = 'number',
            box_cfg = 'wal_retention_period',
            default = 0,
        })),
        -- box.cfg({wal_ext = <...>}) replaces the previous
        -- value without any merging. See explanation why it is
        -- important in the log.modules description.
        ext = enterprise_edition(schema.record({
            old = schema.scalar({
                type = 'boolean',
                -- TODO: This default is applied despite the outer
                -- apply_default_if, because the annotation has no
                -- effect on child schema nodes.
                --
                -- This default is purely informational: lack of the
                -- value doesn't break configuration applying
                -- idempotence.
                -- default = false,
            }),
            new = schema.scalar({
                type = 'boolean',
                -- TODO: See wal.ext.old.
                -- default = false,
            }),
            spaces = schema.map({
                key = schema.scalar({
                    type = 'string',
                }),
                value = schema.record({
                    old = schema.scalar({
                        type = 'boolean',
                        default = false,
                    }),
                    new = schema.scalar({
                        type = 'boolean',
                        default = false,
                    }),
                }),
            }),
        }, {
            box_cfg = 'wal_ext',
            -- TODO: This default doesn't work now. It needs
            -- support of non-scalar schema nodes in
            -- <schema object>:map().
            default = box.NULL,
        })),
    }),
    snapshot = schema.record({
        dir = schema.scalar({
            type = 'string',
            box_cfg = 'memtx_dir',
            box_cfg_nondynamic = true,
            mkdir = true,
            default = 'var/lib/{{ instance_name }}',
        }),
        by = schema.record({
            interval = schema.scalar({
                type = 'number',
                box_cfg = 'checkpoint_interval',
                default = 3600,
            }),
            wal_size = schema.scalar({
                type = 'integer',
                box_cfg = 'checkpoint_wal_threshold',
                default = 1e18,
            }),
        }),
        count = schema.scalar({
            type = 'integer',
            box_cfg = 'checkpoint_count',
            default = 2,
        }),
        snap_io_rate_limit = schema.scalar({
            type = 'number',
            box_cfg = 'snap_io_rate_limit',
            default = box.NULL,
        }),
    }),
    replication = schema.record({
        failover = schema.enum({
            -- No failover ('off').
            --
            -- The leadership in replicasets is controlled using
            -- the database.mode options. It is allowed to have
            -- several writable instances in a replicaset.
            --
            -- The default database.mode is determined as follows.
            --
            -- * 1 instance in a replicaset: 'rw'.
            -- * >1 instances in a replicaset: 'ro'.
            'off',
            -- Manual failover ('manual').
            --
            -- The leadership is controlled using the 'leader'
            -- option of a replicaset. Master-master configuration
            -- is forbidden.
            --
            -- The database.mode option can't be set directly in
            -- the manual failover mode. The leader is configured
            -- in the read-write mode, all the other instances are
            -- read-only.
            'manual',
            -- Automatic leader election ('election').
            --
            -- Uses a RAFT based algorithm for the leader election.
            --
            -- No database.mode or 'leader' options should be set.
            'election',
            -- Automatic leader appointment by an external
            -- failover agent ('supervised').
            --
            -- The default database.mode is 'ro' (but there is an
            -- exception during a replicaset bootstrapping, see
            -- applier/box_cfg.lua).
            --
            -- The failover agent assigns the 'rw' mode to one
            -- instance in the replicaset.
            --
            -- No database.mode or 'leader' options should be set.
            --
            -- TODO: Raise an error if this value is set on
            -- Tarantool Community Edition. The instance side code
            -- that handles failover agent commands is part of
            -- Tarantool Enterprise Edition, so there is no much
            -- sense to enable this mode on the Community Edition.
            'supervised',
        }, {
            default = 'off',
        }),
        -- XXX: needs more validation
        peers = schema.array({
            items = schema.scalar({
                type = 'string',
            }),
            box_cfg = 'replication',
            default = box.NULL,
        }),
        anon = schema.scalar({
            type = 'boolean',
            box_cfg = 'replication_anon',
            default = false,
        }),
        anon_gc_timeout = schema.scalar({
            type = 'number',
            box_cfg = 'replication_anon_gc_timeout',
            default = 60 * 60,
        }),
        threads = schema.scalar({
            type = 'integer',
            box_cfg = 'replication_threads',
            box_cfg_nondynamic = true,
            default = 1,
        }),
        timeout = schema.scalar({
            type = 'number',
            box_cfg = 'replication_timeout',
            default = 1,
        }),
        synchro_timeout = schema.scalar({
            type = 'number',
            box_cfg = 'replication_synchro_timeout',
            default = 5,
        }),
        synchro_queue_max_size = schema.scalar({
            type = 'integer',
            box_cfg = 'replication_synchro_queue_max_size',
            default = 16 * 1024 * 1024,
        }),
        connect_timeout = schema.scalar({
            type = 'number',
            box_cfg = 'replication_connect_timeout',
            default = 30,
        }),
        sync_timeout = schema.scalar({
            type = 'number',
            box_cfg = 'replication_sync_timeout',
            -- The effective default is determined depending on
            -- the compat.box_cfg_replication_sync_timeout option.
            default = box.NULL,
        }),
        sync_lag = schema.scalar({
            type = 'number',
            box_cfg = 'replication_sync_lag',
            default = 10,
        }),
        synchro_quorum = schema.scalar({
            type = 'string, number',
            box_cfg = 'replication_synchro_quorum',
            default = 'N / 2 + 1',
        }),
        skip_conflict = schema.scalar({
            type = 'boolean',
            box_cfg = 'replication_skip_conflict',
            default = false,
        }),
        election_mode = schema.enum({
            'off',
            'voter',
            'manual',
            'candidate',
        }, {
            box_cfg = 'election_mode',
            -- The effective default is determined depending on
            -- the replication.failover option.
            default = box.NULL,
        }),
        election_timeout = schema.scalar({
            type = 'number',
            box_cfg = 'election_timeout',
            default = 5,
        }),
        election_fencing_mode = schema.enum({
            'off',
            'soft',
            'strict',
        }, {
            box_cfg = 'election_fencing_mode',
            default = 'soft',
        }),
        bootstrap_strategy = schema.enum({
            'auto',
            'config',
            'supervised',
            'legacy',
        }, {
            box_cfg = 'bootstrap_strategy',
            default = 'auto',
        }),
    }),
    -- Unlike other sections, credentials contains the append-only
    -- parameters. It means that deletion of a value from the
    -- config doesn't delete the corresponding user/role/privilege.
    credentials = schema.record({
        roles = schema.map({
            -- Name of the role.
            key = schema.scalar({
                type = 'string',
            }),
            value = schema.record({
                -- Privileges granted to the role.
                privileges = schema.array({
                    items = schema.record({
                        permissions = schema.set({
                            'read',
                            'write',
                            'execute',
                            'create',
                            'alter',
                            'drop',
                            'usage',
                            'session',
                        }),
                        universe = schema.scalar({
                            type = 'boolean',
                        }),
                        spaces = schema.array({
                            items = schema.scalar({
                                type = 'string',
                            }),
                        }),
                        functions = schema.array({
                            items = schema.scalar({
                                type = 'string',
                            }),
                        }),
                        sequences = schema.array({
                            items = schema.scalar({
                                type = 'string',
                            }),
                        }),
                        lua_eval = schema.scalar({
                            type = 'boolean',
                        }),
                        lua_call = schema.array({
                            items = schema.scalar({
                                type = 'string',
                            }),
                        }),
                        sql = schema.array({
                            items = schema.scalar({
                                type = 'string',
                                allowed_values = {'all'},
                            }),
                        }),
                    }),
                }),
                -- The given role has all the privileges from
                -- these underlying roles.
                roles = schema.array({
                    items = schema.scalar({
                        type = 'string',
                    }),
                }),
            }),
        }),
        users = schema.map({
            -- Name of the user.
            key = schema.scalar({
                type = 'string',
            }),
            -- Parameters of the user.
            value = schema.record({
                password = schema.scalar({
                    type = 'string',
                }),
                privileges = schema.array({
                    items = schema.record({
                        permissions = schema.set({
                            'read',
                            'write',
                            'execute',
                            'create',
                            'alter',
                            'drop',
                            'usage',
                            'session',
                        }),
                        universe = schema.scalar({
                            type = 'boolean',
                        }),
                        spaces = schema.array({
                            items = schema.scalar({
                                type = 'string',
                            }),
                        }),
                        functions = schema.array({
                            items = schema.scalar({
                                type = 'string',
                            }),
                        }),
                        sequences = schema.array({
                            items = schema.scalar({
                                type = 'string',
                            }),
                        }),
                        lua_eval = schema.scalar({
                            type = 'boolean',
                        }),
                        lua_call = schema.array({
                            items = schema.scalar({
                                type = 'string',
                            }),
                        }),
                        sql = schema.array({
                            items = schema.scalar({
                                type = 'string',
                                allowed_values = {'all'},
                            }),
                        }),
                    }),
                }),
                -- The given user has all the privileges from
                -- these underlying roles.
                roles = schema.array({
                    items = schema.scalar({
                        type = 'string',
                    }),
                }),
            }),
        }),
    }),
    app = schema.record({
        file = schema.scalar({
            type = 'string',
        }),
        module = schema.scalar({
            type = 'string',
        }),
        cfg = schema.map({
            key = schema.scalar({
                type = 'string',
            }),
            value = schema.scalar({
                type = 'any',
            }),
        }),
    }, {
        validate = function(app, w)
            if app.file ~= nil and app.module ~= nil then
                w.error('Fields file and module cannot appear at the same time')
            end
        end,
    }),
    feedback = schema.record({
        enabled = schema.scalar({
            type = 'boolean',
            box_cfg = 'feedback_enabled',
            default = true,
            apply_default_if = feedback_apply_default_if,
            validate = feedback_validate,
        }),
        crashinfo = schema.scalar({
            type = 'boolean',
            box_cfg = 'feedback_crashinfo',
            default = true,
            apply_default_if = feedback_apply_default_if,
            validate = feedback_validate,
        }),
        host = schema.scalar({
            type = 'string',
            box_cfg = 'feedback_host',
            default = 'https://feedback.tarantool.io',
            apply_default_if = feedback_apply_default_if,
            validate = feedback_validate,
        }),
        metrics_collect_interval = schema.scalar({
            type = 'number',
            box_cfg = 'feedback_metrics_collect_interval',
            default = 60,
            apply_default_if = feedback_apply_default_if,
            validate = feedback_validate,
        }),
        send_metrics = schema.scalar({
            type = 'boolean',
            box_cfg = 'feedback_send_metrics',
            default = true,
            apply_default_if = feedback_apply_default_if,
            validate = feedback_validate,
        }),
        interval = schema.scalar({
            type = 'number',
            box_cfg = 'feedback_interval',
            default = 3600,
            apply_default_if = feedback_apply_default_if,
            validate = feedback_validate,
        }),
        metrics_limit = schema.scalar({
            type = 'integer',
            box_cfg = 'feedback_metrics_limit',
            default = 1024 * 1024,
            apply_default_if = feedback_apply_default_if,
            validate = feedback_validate,
        }),
    }),
    flightrec = schema.record({
        enabled = enterprise_edition(schema.scalar({
            type = 'boolean',
            box_cfg = 'flightrec_enabled',
            default = false,
        })),
        logs_size = enterprise_edition(schema.scalar({
            type = 'integer',
            box_cfg = 'flightrec_logs_size',
            default = 10485760,
        })),
        logs_max_msg_size = enterprise_edition(schema.scalar({
            type = 'integer',
            box_cfg = 'flightrec_logs_max_msg_size',
            default = 4096,
        })),
        logs_log_level = enterprise_edition(schema.scalar({
            type = 'integer',
            box_cfg = 'flightrec_logs_log_level',
            default = 6,
            allowed_values = {0, 1, 2, 3, 4, 5, 6, 7},
        })),
        metrics_interval = enterprise_edition(schema.scalar({
            type = 'number',
            box_cfg = 'flightrec_metrics_interval',
            default = 1.0,
        })),
        metrics_period = enterprise_edition(schema.scalar({
            type = 'number',
            box_cfg = 'flightrec_metrics_period',
            default = 60 * 3,
        })),
        requests_size = enterprise_edition(schema.scalar({
            type = 'integer',
            box_cfg = 'flightrec_requests_size',
            default = 10485760,
        })),
        requests_max_req_size = enterprise_edition(schema.scalar({
            type = 'integer',
            box_cfg = 'flightrec_requests_max_req_size',
            default = 16384,
        })),
        requests_max_res_size = enterprise_edition(schema.scalar({
            type = 'integer',
            box_cfg = 'flightrec_requests_max_res_size',
            default = 16384,
        })),
    }),
    security = schema.record({
        auth_type = schema.enum({
            'chap-sha1',
            'pap-sha256',
        }, {
            box_cfg = 'auth_type',
            default = 'chap-sha1',
            validate = function(auth_type, w)
                if auth_type ~= 'chap-sha1' and
                        tarantool.package ~= 'Tarantool Enterprise' then
                    w.error('"chap-sha1" is the only authentication method ' ..
                            '(auth_type) available in Tarantool Community ' ..
                            'Edition (%q requested)', auth_type)
                end
            end,
        }),
        auth_delay = enterprise_edition(schema.scalar({
            type = 'number',
            default = 0,
            box_cfg = 'auth_delay',
        })),
        auth_retries = enterprise_edition(schema.scalar({
            type = 'integer',
            default = 0,
            box_cfg = 'auth_retries',
        })),
        disable_guest = enterprise_edition(schema.scalar({
            type = 'boolean',
            default = false,
            box_cfg = 'disable_guest',
        })),
        secure_erasing = enterprise_edition(schema.scalar({
            type = 'boolean',
            default = false,
            box_cfg = 'secure_erasing',
        })),
        password_lifetime_days = enterprise_edition(schema.scalar({
            type = 'integer',
            default = 0,
            box_cfg = 'password_lifetime_days',
        })),
        password_min_length = enterprise_edition(schema.scalar({
            type = 'integer',
            default = 0,
            box_cfg = 'password_min_length',
        })),
        password_enforce_uppercase = enterprise_edition(schema.scalar({
            type = 'boolean',
            default = false,
            box_cfg = 'password_enforce_uppercase',
        })),
        password_enforce_lowercase = enterprise_edition(schema.scalar({
            type = 'boolean',
            default = false,
            box_cfg = 'password_enforce_lowercase',
        })),
        password_enforce_digits = enterprise_edition(schema.scalar({
            type = 'boolean',
            default = false,
            box_cfg = 'password_enforce_digits',
        })),
        password_enforce_specialchars = enterprise_edition(schema.scalar({
            type = 'boolean',
            default = false,
            box_cfg = 'password_enforce_specialchars',
        })),
        password_history_length = enterprise_edition(schema.scalar({
            type = 'integer',
            default = 0,
            box_cfg = 'password_history_length',
        })),
    }),
    metrics = schema.record({
        -- Metrics doesn't have box_cfg annotation, because currently nested
        -- options and maps/arrays defaults are not supported.
        include = schema.set({
            'all',
            'network',
            'operations',
            'system',
            'replicas',
            'info',
            'slab',
            'runtime',
            'memory',
            'spaces',
            'fibers',
            'cpu',
            'vinyl',
            'memtx',
            'luajit',
            'clock',
            'event_loop',
        }),
        exclude = schema.set({
            'all',
            'network',
            'operations',
            'system',
            'replicas',
            'info',
            'slab',
            'runtime',
            'memory',
            'spaces',
            'fibers',
            'cpu',
            'vinyl',
            'memtx',
            'luajit',
            'clock',
            'event_loop',
        }),
        labels = schema.map({
            key = schema.scalar({type = 'string'}),
            value = schema.scalar({type = 'string'}),
        }),
    }),
    sharding = schema.record({
        -- Instance vshard options.
        zone = schema.scalar({type = 'integer'}),
        -- Replicaset vshard options.
        lock = schema.scalar({
            type = 'boolean',
            validate = function(data, w)
                local scope = w.schema.computed.annotations.scope
                if data == nil or scope == nil then
                    return
                end
                if scope == 'instance' then
                    w.error('sharding.lock cannot be defined in the instance '..
                            'scope')
                end
            end,
        }),
        weight = schema.scalar({
            type = 'number',
            default = 1,
            validate = function(data, w)
                local scope = w.schema.computed.annotations.scope
                if data == nil or scope == nil then
                    return
                end
                if scope == 'instance' then
                    w.error('sharding.weight cannot be defined in the ' ..
                            'instance scope')
                end
            end,
        }),
        -- TODO: Add validate.
        roles = schema.set({
            'router',
            'storage',
            'rebalancer',
        }),
        -- Global vshard options.
        shard_index = schema.scalar({
            type = 'string',
            default = 'bucket_id',
            validate = function(data, w)
                local scope = w.schema.computed.annotations.scope
                if data == nil or scope == nil then
                    return
                end
                if scope ~= 'global' then
                    w.error('sharding.shard_index should be a defined in '..
                            'global scope')
                end
            end,
        }),
        bucket_count = schema.scalar({
            type = 'integer',
            default = 3000,
            validate = function(data, w)
                local scope = w.schema.computed.annotations.scope
                if data == nil or scope == nil then
                    return
                end
                if scope ~= 'global' then
                    w.error('sharding.bucket_count should be a defined in '..
                            'global scope')
                end
            end,
        }),
        rebalancer_mode = schema.enum({
            'manual',
            'auto',
            'off',
        }, {
            default = 'auto',
            validate = function(data, w)
                local scope = w.schema.computed.annotations.scope
                if data == nil or scope == nil then
                    return
                end
                if scope ~= 'global' then
                    w.error('sharding.rebalancer_enabled must be defined in ' ..
                            'the global scope.')
                end
            end,
        }),
        rebalancer_disbalance_threshold = schema.scalar({
            type = 'number',
            default = 1,
            validate = function(data, w)
                local scope = w.schema.computed.annotations.scope
                if data == nil or scope == nil then
                    return
                end
                if scope ~= 'global' then
                    w.error('sharding.rebalancer_disbalance_threshold should '..
                            'be a defined in global scope')
                end
            end,
        }),
        rebalancer_max_receiving = schema.scalar({
            type = 'integer',
            default = 100,
            validate = function(data, w)
                local scope = w.schema.computed.annotations.scope
                if data == nil or scope == nil then
                    return
                end
                if scope ~= 'global' then
                    w.error('sharding.rebalancer_max_receiving should '..
                            'be a defined in global scope')
                end
            end,
        }),
        rebalancer_max_sending = schema.scalar({
            type = 'integer',
            default = 1,
            validate = function(data, w)
                local scope = w.schema.computed.annotations.scope
                if data == nil or scope == nil then
                    return
                end
                if scope ~= 'global' then
                    w.error('sharding.rebalancer_max_sending should '..
                            'be a defined in global scope')
                end
            end,
        }),
        sync_timeout = schema.scalar({
            type = 'number',
            default = 1,
            validate = function(data, w)
                local scope = w.schema.computed.annotations.scope
                if data == nil or scope == nil then
                    return
                end
                if scope ~= 'global' then
                    w.error('sharding.sync_timeout should be a defined in '..
                            'global scope')
                end
            end,
        }),
        connection_outdate_delay = schema.scalar({
            type = 'number',
            validate = function(data, w)
                local scope = w.schema.computed.annotations.scope
                if data == nil or scope == nil then
                    return
                end
                if scope ~= 'global' then
                    w.error('sharding.connection_outdate_delay should be a '..
                            'defined in global scope')
                end
            end,
        }),
        failover_ping_timeout = schema.scalar({
            type = 'number',
            default = 5,
            validate = function(data, w)
                local scope = w.schema.computed.annotations.scope
                if data == nil or scope == nil then
                    return
                end
                if scope ~= 'global' then
                    w.error('sharding.failover_ping_timeout should be a '..
                            'defined in global scope')
                end
            end,
        }),
        discovery_mode = schema.enum({
            'on',
            'off',
            'once',
        }, {
            default = 'on',
            validate = function(data, w)
                local scope = w.schema.computed.annotations.scope
                if data == nil or scope == nil then
                    return
                end
                if scope ~= 'global' then
                    w.error('sharding.discovery_mode should be a defined in '..
                            'global scope')
                end
            end,
        }),
        sched_ref_quota = schema.scalar({
            type = 'number',
            default = 300,
            validate = function(data, w)
                local scope = w.schema.computed.annotations.scope
                if data == nil or scope == nil then
                    return
                end
                if scope ~= 'global' then
                    w.error('sharding.sched_ref_quota should be a defined ' ..
                            'in global scope')
                end
            end,
        }),
        sched_move_quota = schema.scalar({
            type = 'number',
            default = 1,
            validate = function(data, w)
                local scope = w.schema.computed.annotations.scope
                if data == nil or scope == nil then
                    return
                end
                if scope ~= 'global' then
                    w.error('sharding.sched_move_quota should be a defined ' ..
                            'in global scope')
                end
            end,
        }),
    }, {
        validate = function(data, w)
            -- Forbid sharding.roles in instance scope.
            local scope = w.schema.computed.annotations.scope
            if data.roles ~= nil and scope == 'instance' then
                w.error('sharding.roles cannot be defined in the instance ' ..
                        'scope')
            end
            -- Make sure that if the rebalancer role is present, the storage
            -- role is also present.
            if data.roles ~= nil then
                local has_storage = false
                local has_rebalancer = false
                for _, role in pairs(data.roles) do
                    has_storage = has_storage or role == 'storage'
                    has_rebalancer = has_rebalancer or role == 'rebalancer'
                end
                if has_rebalancer and not has_storage then
                    w.error('The rebalancer role cannot be present without ' ..
                            'the storage role')
                end
            end
        end,
    }),
    audit_log = enterprise_edition(schema.record({
        -- The same as the destination for the logger, audit logger destination
        -- is handled separately in the box_cfg applier, so there are no
        -- explicit box_cfg and box_cfg_nondynamic annotations.
        --
        -- The reason is that there is no direct-no-transform
        -- mapping from, say, `audit_log.file` to `box_cfg.audit_log`.
        -- The applier should add the `file:` prefix.
        to = enterprise_edition(schema.enum({
            'devnull',
            'file',
            'pipe',
            'syslog',
        }, {
            default = 'devnull',
        })),
        file = enterprise_edition(schema.scalar({
            type = 'string',
            -- The mk_parent_dir annotation is not present here,
            -- because otherwise the directory would be created
            -- unconditionally. Instead, mkdir applier creates it
            -- if audit_log.to is 'file'.
            default = 'var/log/{{ instance_name }}/audit.log',
        })),
        pipe = enterprise_edition(schema.scalar({
            type = 'string',
            default = box.NULL,
        })),
        syslog = schema.record({
            identity = enterprise_edition(schema.scalar({
                type = 'string',
                default = 'tarantool',
            })),
            facility = enterprise_edition(schema.scalar({
                type = 'string',
                default = 'local7',
            })),
            server = enterprise_edition(schema.scalar({
                type = 'string',
                -- The logger tries /dev/log and then
                -- /var/run/syslog if no server is provided.
                default = box.NULL,
            })),
        }),
        nonblock = enterprise_edition(schema.scalar({
            type = 'boolean',
            box_cfg = 'audit_nonblock',
            box_cfg_nondynamic = true,
            default = false,
        })),
        format = enterprise_edition(schema.enum({
            'plain',
            'json',
            'csv',
        }, {
            box_cfg = 'audit_format',
            box_cfg_nondynamic = true,
            default = 'json',
        })),
        -- The reason for the absence of the box_cfg and box_cfg_nondynamic
        -- annotations is that this setting needs to be converted to a string
        -- before being set to 'audit_filter'. This will be done in box_cfg
        -- applier.
        --
        -- TODO: Add a validation and a default value. Currently, the audit_log
        -- validation can catch the setting of the option in the CE, but adding
        -- its own validation seems more appropriate.
        filter = schema.set({
            -- Events.
            "audit_enable",
            "custom",
            "auth_ok",
            "auth_fail",
            "disconnect",
            "user_create",
            "user_drop",
            "role_create",
            "role_drop",
            "user_enable",
            "user_disable",
            "user_grant_rights",
            "user_revoke_rights",
            "role_grant_rights",
            "role_revoke_rights",
            "password_change",
            "access_denied",
            "eval",
            "call",
            "space_select",
            "space_create",
            "space_alter",
            "space_drop",
            "space_insert",
            "space_replace",
            "space_delete",
            -- Groups of events.
            "none",
            "all",
            "audit",
            "auth",
            "priv",
            "ddl",
            "dml",
            "data_operations",
            "compatibility",
        }),
        spaces = enterprise_edition(schema.array({
            items = schema.scalar({
                type = 'string',
            }),
            box_cfg = 'audit_spaces',
            box_cfg_nondynamic = true,
            default = box.NULL,
        })),
        extract_key = enterprise_edition(schema.scalar({
            type = 'boolean',
            box_cfg = 'audit_extract_key',
            box_cfg_nondynamic = true,
            default = false,
        })),
    })),
    roles_cfg = schema.map({
        key = schema.scalar({type = 'string'}),
        value = schema.scalar({type = 'any'}),
    }),
    roles = schema.array({
        items = schema.scalar({type = 'string'})
    }),
    -- Options of the failover coordinator service.
    --
    -- TODO: Allow only in the global scope.
    failover = schema.record({
        probe_interval = schema.scalar({
            type = 'number',
            default = 10,
        }),
        connect_timeout = schema.scalar({
            type = 'number',
            default = 1,
        }),
        call_timeout = schema.scalar({
            type = 'number',
            default = 1,
        }),
        lease_interval = schema.scalar({
            type = 'number',
            default = 30,
        }),
        renew_interval = schema.scalar({
            type = 'number',
            default = 10,
        }),
        -- Configure how to work with a remote storage, where
        -- failover coordinators leave its state information.
        --
        -- Currently, the state storage is the same as a remote
        -- configuration storage. The URIs and connection options
        -- are configured in the `config.etcd` or the
        -- `config.storage` section.
        stateboard = schema.record({
            -- How often information in the stateboard is updated.
            renew_interval = schema.scalar({
                type = 'number',
                default = 2,
            }),
            -- How long a transient state information is stored.
            --
            -- Beware: it affects how fast a coordinator assumes
            -- that the previous active coordinator is gone.
            --
            -- The option should be smaller than
            -- failover.lease_interval. Otherwise the coordinator
            -- switching causes replicaset leaders to go to the
            -- read-only mode for some time interval.
            keepalive_interval = schema.scalar({
                type = 'number',
                default = 10,
            }),
        }),
        replicasets = schema.map({
            -- Name of the replica.
            key = schema.scalar({
                type = 'string',
            }),
            value = schema.record({
                -- Priorities for the supervised failover mode.
                priority = schema.map({
                    key = schema.scalar({
                        type = 'string',
                    }),
                    value = schema.scalar({
                        type = 'number',
                    }),
                }),
            }),
        }),
    }),
    -- Compatibility options.
    compat = schema.record({
        json_escape_forward_slash = schema.enum({
            'old',
            'new',
        }, {
            default = 'new',
        }),
        yaml_pretty_multiline = schema.enum({
            'old',
            'new',
        }, {
            default = 'new',
        }),
        fiber_channel_close_mode = schema.enum({
            'old',
            'new',
        }, {
            default = 'new',
        }),
        box_cfg_replication_sync_timeout = schema.enum({
            'old',
            'new',
        }, {
            default = 'new',
        }),
        sql_seq_scan_default = schema.enum({
            'old',
            'new',
        }, {
            default = 'new',
        }),
        fiber_slice_default = schema.enum({
            'old',
            'new',
        }, {
            default = 'new',
        }),
        box_info_cluster_meaning = schema.enum({
            'old',
            'new',
        }, {
            default = 'new',
        }),
        binary_data_decoding = schema.enum({
            'old',
            'new',
        }, {
            default = 'new',
        }),
        box_tuple_new_vararg = schema.enum({
            'old',
            'new',
        }, {
            default = 'new',
        }),
        box_session_push_deprecation = schema.enum({
            'old',
            'new',
        }, {
            default = 'old',
        }),
        sql_priv = schema.enum({
            'old',
            'new',
        }, {
            default = 'new',
        }),
        c_func_iproto_multireturn = schema.enum({
            'old',
            'new',
        }, {
            default = 'new',
        }),
        box_space_execute_priv = schema.enum({
            'old',
            'new',
        }, {
            default = 'new',
        }),
        box_tuple_extension = schema.enum({
            'old',
            'new',
        }, {
            default = 'new',
        }),
        box_space_max = schema.enum({
            'old',
            'new',
        }, {
            default = 'new',
        }),
        box_error_unpack_type_and_code = schema.enum({
            'old',
            'new',
        }, {
            default = 'old',
        }),
        box_error_serialize_verbose = schema.enum({
            'old',
            'new',
        }, {
            default = 'old',
        }),
        console_session_scope_vars = schema.enum({
            'old',
            'new',
        }, {
            default = 'old',
        }),
        box_consider_system_spaces_synchronous = schema.enum({
            'old',
            'new',
        }, {
            default = 'old',
        }),
        wal_cleanup_delay_deprecation = schema.enum({
            'old',
            'new',
        }, {
            default = 'old',
        }),
        replication_synchro_timeout = schema.enum({
            'old',
            'new',
        }, {
            default = 'old',
        }),
    }),
    -- Instance labels.
    labels = schema.map({
        key = schema.scalar({
            type = 'string',
        }),
        value = schema.scalar({
            type = 'string',
        }),
    }),
}, {
    -- This kind of validation cannot be implemented as the
    -- 'validate' annotation of a particular schema node. There
    -- are two reasons:
    --
    -- * Missed fields are not validated.
    -- * The outmost instance config record is marked with the
    --   'scope' annotation (when the instance config is part of
    --   the cluster config), but this annotation is not easy to
    --   reach from the 'validate' function of a nested schema
    --   node.
    validate = validate_outmost_record,
}), {
    methods = {
        instance_uri = instance_uri,
        apply_vars = apply_vars,
        base_dir = base_dir,
        prepare_file_path = prepare_file_path,
    },
})
