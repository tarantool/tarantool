local fun = require('fun')
local textutils = require('internal.config.utils.textutil')

local function for_each_list_item(s, f)
    return table.concat(fun.iter(s:split('\n- ')):map(f):totable(), '\n- ')
end

local function for_each_paragraph(s, f)
    return table.concat(fun.iter(s:split('\n\n')):map(f):totable(), '\n\n')
end

local function format_text(s)
    return for_each_paragraph(textutils.dedent(s), function(paragraph)
        -- Strip line breaks if the paragraph is not a list.
        if paragraph:startswith('- ') then
            -- Strip newlines in each list item.
            return '- ' .. for_each_list_item(paragraph:sub(3),
                                              function(list_item)
                return textutils.toline(list_item)
            end)
        else
            return textutils.toline(paragraph)
        end
    end)
end

local M = {}

-- {{{ <uri>.params configuration

M['<uri>.params'] = format_text([[
    SSL parameters required for encrypted connections.
]])

M['<uri>.params.ssl_ca_file'] = format_text([[
    (Optional) A path to a trusted certificate authorities (CA) file. If not
    set, the peer won't be checked for authenticity.

    Both a server and a client can use the ssl_ca_file parameter:

    - If it's on the server side, the server verifies the client.
    - If it's on the client side, the client verifies the server.
    - If both sides have the CA files, the server and the client verify each
    other.
]])

M['<uri>.params.ssl_cert_file'] = format_text([[
    A path to an SSL certificate file:

    - For a server, it's mandatory.
    - For a client, it's mandatory if the ssl_ca_file parameter is set for a
    server; otherwise, optional.
]])

M['<uri>.params.ssl_ciphers'] = format_text([[
    (Optional) A colon-separated (:) list of SSL cipher suites the connection
    can use. Note that the list is not validated: if a cipher suite is unknown,
    Tarantool ignores it, doesn't establish the connection, and writes to the
    log that no shared cipher was found.
]])

M['<uri>.params.ssl_key_file'] = format_text([[
    A path to a private SSL key file:

    - For a server, it's mandatory.
    - For a client, it's mandatory if the `ssl_ca_file` parameter is set for a
    server; otherwise, optional.

    If the private key is encrypted, provide a password for it in the
    `ssl_password` or `ssl_password_file` parameter
]])

M['<uri>.params.ssl_password'] = format_text([[
    (Optional) A password for an encrypted private SSL key provided using
    `ssl_key_file`. Alternatively, the password can be provided in
    `ssl_password_file`.

    Tarantool applies the `ssl_password` and `ssl_password_file` parameters in
    the following order:

    - If `ssl_password` is provided, Tarantool tries to decrypt the private key
    with it.
    - If `ssl_password` is incorrect or isn't provided, Tarantool tries all
    passwords from `ssl_password_file` one by one in the order they are written.
    - If `ssl_password` and all passwords from `ssl_password_file` are
    incorrect, or none of them is provided, Tarantool treats the private key as
    unencrypted.
]])

M['<uri>.params.ssl_password_file'] = format_text([[
    (Optional) A text file with one or more passwords for encrypted private
    SSL keys provided using `ssl_key_file` (each on a separate line).
    Alternatively, the password can be provided in `ssl_password`.
]])

M['<uri>.params.transport'] = format_text([[
    Allows you to enable traffic encryption for client-server communications
    over binary connections. In a Tarantool cluster, one instance might act
    as the server that accepts connections from other instances and the client
    that connects to other instances.

    `<uri>.params.transport` accepts one of the following values:

    - `plain` (default): turn off traffic encryption,
    - `ssl`: encrypt traffic by using the TLS 1.2 protocol (EE only).
]])

-- }}} <uri>.params configuration

-- {{{ app configuration

M['app'] = format_text([[
    Using Tarantool as an application server, you can run your own Lua
    applications. In the `app` section, you can load the application and
    provide an application configuration in the `app.cfg` section.
]])

M['app.cfg'] = format_text([[
    A configuration of the application loaded using `app.file` or `app.module`.
]])

M['app.cfg.*'] = format_text([[
    Specify `file` (a path to a Lua file to load an application from) or
    `module` (a Lua module to load an application from).
]])

M['app.file'] = 'A path to a Lua file to load an application from.'

M['app.module'] = 'A Lua module to load an application from.'

-- }}} app configuration

-- {{{ audit_log configuration

M['audit_log'] = format_text([[
    The `audit_log` section defines configuration parameters related to
    audit logging.
]])

M['audit_log.extract_key'] = format_text([[
    If set to `true`, the audit subsystem extracts and prints only the primary
    key instead of full tuples in DML events (`space_insert`, `space_replace`,
    `space_delete`). Otherwise, full tuples are logged. The option may be
    useful in case tuples are big.
]])

M['audit_log.file'] = format_text([[
    Specify a file for the audit log destination. You can set the `file` type
    using the audit_log.to option. If you write logs to a file, Tarantool
    reopens the audit log at SIGHUP.
]])

M['audit_log.filter'] = format_text([[
    Enable logging for a specified subset of audit events. This option accepts
    the following values:

    - Event names (for example, password_change). For details, see Audit log
    events.
    - Event groups (for example, audit). For details, see Event groups.

    The option contains either one value from Possible values section or a
    combination of them.

    To enable custom audit log events, specify the custom value in this option.
]])

M['audit_log.filter.*'] = format_text([[
    Specify a subset of audit events to log by providing a value
    from the allowed list of events or groups.
]])

M['audit_log.format'] = 'Specify a format that is used for the audit log.'

M['audit_log.nonblock'] = format_text([[
    Specify the logging behavior if the system is not ready to write.
    If set to `true`, Tarantool does not block during logging if the system
    is non-writable and writes a message instead. Using this value may
    improve logging performance at the cost of losing some log messages.
]])

M['audit_log.pipe'] = format_text([[
    Specify a pipe for the audit log destination. You can set the `pipe` type
    using the audit_log.to option. If log is a program, its pid is stored in
    the `audit.pid` field. You need to send it a signal to rotate logs.
]])

M['audit_log.spaces'] = format_text([[
    The array of space names for which data operation events (`space_select`,
    `space_insert`, `space_replace`, `space_delete`) should be logged. The array
    accepts string values. If set to box.NULL, the data operation events are
    logged for all spaces.
]])

M['audit_log.spaces.*'] = format_text([[
    A specific space name in the array for which data operation events are
    logged. Each entry must be a string representing the name of the space
    to monitor.

    Example:

    `spaces: [bands, singers]`, only the events of `bands` and `singers` spaces
    are logged.
]])

M['audit_log.to'] = 'Enable audit logging and define the log location.'

M['audit_log.syslog'] = format_text([[
    This module allows configuring the system logger (syslog) for audit
    logs in Tarantool. It provides options for specifying the syslog server,
    facility, and identity for logging messages.
]])

M['audit_log.syslog.facility'] = format_text([[
    Specify a system logger keyword that tells syslogd where to send the
    message. You can enable logging to a system logger using the
    `audit_log.to` option.
]])

M['audit_log.syslog.identity'] = format_text([[
    Specify an application name to show in logs. You can enable logging
    to a system logger using the `audit_log.to` option.
]])

M['audit_log.syslog.server'] = format_text([[
    Set a location for the syslog server. It can be a Unix socket path
    starting with "unix:" or an ipv4 port number. You can enable logging
    to a system logger using the `audit_log.to` option.
]])

-- }}} audit_log configuration

-- {{{ compat configuration

M['compat'] = format_text([[
    The `compat` section defines values of the compat module options.
]])

M['compat.binary_data_decoding'] = format_text([[
    Define how to store binary data fields in Lua after decoding:

    - `new`: as varbinary objects
    - `old`: as plain strings
]])

M['compat.box_cfg_replication_sync_timeout'] = format_text([[
    Set a default replication sync timeout:

    - `new`: 0
    - `old`: 300 seconds
]])

M['compat.box_consider_system_spaces_synchronous'] = format_text([[
    Controls whether to consider system spaces synchronous when the
    synchronous queue is claimed, regardless of the user-provided `is_sync`
    option. Either enables synchronous replication for system spaces when the
    synchronous queue is claimed, overriding the user-provided 'is_sync' space
    option, or falls back to the user-provided 'is_sync' space option.
]])

M['compat.box_error_serialize_verbose'] = format_text([[
    Set the verbosity of error objects serialization:

    - `new`: serialize the error message together with other potentially
    useful fields
    - `old`: serialize only the error message
]])

M['compat.box_error_unpack_type_and_code'] = format_text([[
    Whether to show error fields in `box.error.unpack()`:

    - `new`: do not show `base_type` and `custom_type` fields; do not show the
    `code` field if it is 0. Note that `base_type` is still accessible for an
    error object
    - `old`: show all fields
]])

M['compat.box_info_cluster_meaning'] = format_text([[
    Define the behavior of `box.info.cluster`:

    - `new`: show the entire cluster
    - `old`: show the current replica set
]])

M['compat.box_session_push_deprecation'] = format_text([[
    Whether to raise errors on attempts to call the deprecated function
    `box.session.push`:

    - `new`: raise an error
    - `old`: do not raise an error
]])

M['compat.box_space_execute_priv'] = format_text([[
    Whether the `execute` privilege can be granted on spaces:

    - `new`: an error is raised
    - `old`: the privilege can be granted with no actual effect
]])

M['compat.box_space_max'] = format_text([[
    Set the maximum space identifier (`box.schema.SPACE_MAX`):

    - `new`: 2147483646
    - `old`: 2147483647
]])

M['compat.box_tuple_extension'] = format_text([[
    Controls `IPROTO_FEATURE_CALL_RET_TUPLE_EXTENSION` and
    `IPROTO_FEATURE_CALL_ARG_TUPLE_EXTENSION` feature bits that define tuple
    encoding in iproto call and eval requests.

    - `new`: tuples with formats are encoded as `MP_TUPLE`
    - `old`: tuples with formats are encoded as `MP_ARRAY`
]])

M['compat.box_tuple_new_vararg'] = format_text([[
    Controls how `box.tuple.new` interprets an argument list:

    - `new`: as a value with a tuple format
    - `old`: as an array of tuple fields
]])

M['compat.c_func_iproto_multireturn'] = format_text([[
    Controls wrapping of multiple results of a stored C function when returning
    them via iproto:

    - `new`: return without wrapping (consistently with a local call via
    `box.func`)
    - `old`: wrap results into a MessagePack array
]])

M['compat.console_session_scope_vars'] = format_text([[
    Whether a console session has its own variable scope.

    In the old behavior, all the non-local variable assignments from the console
    are written to globals. In the new behavior, they're written to a variable
    scope attached to the console session.

    Also, a couple of built-in modules are added into the initial console
    variable scope in the new behavior.
]])

M['compat.fiber_channel_close_mode'] = format_text([[
    Define the behavior of fiber channels after closing:

    - `new`: mark the channel read-only
    - `old`: destroy the channel object
]])

M['compat.fiber_slice_default'] = format_text([[
    Define the maximum fiber execution time without a yield:

    - `new`: `{warn = 0.5, err = 1.0}`
    - `old`: infinity (no warnings or errors raised)
]])

M['compat.json_escape_forward_slash'] = format_text([[
    Whether to escape the forward slash symbol "/" using a backslash
    in a `json.encode()` result:

    - `new`: do not escape the forward slash
    - `old`: escape the forward slash
]])

M['compat.replication_synchro_timeout'] = format_text([[
    The `compat.replication_synchro_timeout` option controls transaction
    rollback due to `replication.synchro_timeout`.

    - `old`: the old behavior: unconfirmed synchronous transactions are rolled
    back after a `replication.synchro_timeout`.
    - `new`: A synchronous transaction can remain in the synchro queue
    indefinitely until it reaches a quorum of confirmations.
    `replication.synchro_timeout` is used only to wait confirmation
    in promote/demote and gc-checkpointing. If some transaction in limbo
    did not have time to commit within `replication_synchro_timeput`,
    the corresponding operation: promote/demote or gc-checkpointing
    can be aborted automatically.
]])

M['compat.sql_priv'] = format_text([[
    Whether to enable access checks for SQL requests over iproto:

    - `new`: check the user's access permissions
    - `old`: allow any user to execute SQL over iproto
]])

M['compat.sql_seq_scan_default'] = format_text([[
    Controls the default value of the `sql_seq_scan` session setting:

    - `new`: false
    - `old`: true
]])

M['compat.wal_cleanup_delay_deprecation'] = format_text([[
    Whether option 'wal_cleanup_delay' can be used.

    The old behavior is to log a deprecation warning when it's used, the new
    one - raise an error.

    If Tarantool participates in a cluster, xlogs needed for other replicas will
    be retained by persistent WAL GC.
]])

M['compat.yaml_pretty_multiline'] = format_text([[
    Whether to encode in block scalar style all multiline strings or ones
    containing the `\n\n` substring:

    - `new`: all multiline strings
    - `old`: only strings containing the `\n\n` substring
]])

-- }}} compat configuration

-- {{{ conditional configuration

M['conditional'] = format_text([[
    The `conditional` section defines the configuration parts that apply to
    instances that meet certain conditions.
]])

M['conditional.*'] = format_text([[
    `conditional.if`: Defines a section where configuration options apply only
    if specified conditions are met.

    Conditions can include `tarantool_version` and support comparison operators
    (`>`, `<`, `>=`, `<=`, `==`, `!=`), as well as logical operators
    (`||`, `&&`) and parentheses for grouping.
]])

M['conditional.*.*'] = format_text([[
    `conditional.if.*`: Defines a specific condition and the configuration
    options applied when the condition is met.

    Each condition compares the `tarantool_version` using operators
    (`<`, `>`, `<=`, `>=`, `==`, `!=`) and supports defining labels or other
    options that are applied only if the condition holds true.
]])

-- }}} conditional configuration

-- {{{ config configuration

M['config'] = format_text([[
    The `config` section defines various parameters related to
    centralized configuration.
]])

-- {{{ config.context configuration

M['config.context'] = format_text([[
    Specify how to load settings from external storage. For example,
    this option can be used to load passwords from safe storage. You can
    find examples in the Loading secrets from safe storage section.
]])

M['config.context.*'] = format_text([[
    The name of an entity that identifies a configuration value to load.
]])

M['config.context.*.env'] = format_text([[
    The name of an environment variable to load a configuration value from.
    To load a configuration value from an environment variable, set
    `config.context.<name>.from` to `env`.
]])

M['config.context.*.file'] = format_text([[
    The path to a file to load a configuration value from.To load a
    configuration value from a file, set `config.context.<name>.from` to `file`.
]])

M['config.context.*.from'] = format_text([[
    The type of storage to load a configuration value from. There are the
    following storage types:

    - `file`: load a configuration value from a file. In this case, you
    need to specify the path to the file using `config.context.<name>.file`
    - `env`: load a configuration value from an environment variable.
    In this case, specify the environment variable name using
    `config.context.<name>.env`
]])

M['config.context.*.rstrip'] = format_text([[
    (Optional) Whether to strip whitespace characters and newlines
    from the end of data.
]])

-- }}} config.context configuration

-- {{{ config.etcd configuration

M['config.etcd'] = format_text([[
    This section describes options related to providing connection
    settings to a centralized etcd-based storage. If `replication.failover`
    is set to `supervised`, Tarantool also uses etcd to maintain the state
    of failover coordinators.
]])

M['config.etcd.endpoints'] = format_text([[
    The list of endpoints used to access an etcd server.
]])

M['config.etcd.endpoints.*'] = format_text([[
    etcd endpoint name.

    For example: `http://localhost:2379`.
]])

M['config.etcd.http'] = 'HTTP client options.'

M['config.etcd.http.request'] = 'HTTP client request options.'

M['config.etcd.http.request.timeout'] = format_text([[
    A time period required to process an HTTP request to an etcd server:
    from sending a request to receiving a response.
]])
M['config.etcd.http.request.unix_socket'] = format_text([[
    A Unix domain socket used to connect to an etcd server.
]])

M['config.etcd.password'] = 'A password used for authentication.'

M['config.etcd.prefix'] = format_text([[
    A key prefix used to search a configuration on an etcd server.
    Tarantool searches keys by the following path: `<prefix>/config/*`.
    Note that `<prefix>` should start with a slash (`/`).
]])

M['config.etcd.ssl'] = 'TLS options.'

M['config.etcd.ssl.ca_file'] = format_text([[
    A path to a trusted certificate authorities (CA) file.
]])

M['config.etcd.ssl.ca_path'] = format_text([[
    A path to a directory holding certificates to verify the peer with.
]])

M['config.etcd.ssl.ssl_cert'] = 'A path to an SSL certificate file.'

M['config.etcd.ssl.ssl_key'] = 'A path to a private SSL key file.'

M['config.etcd.ssl.verify_host'] = format_text([[
    Enable verification of the certificate's name (CN) against
    the specified host.
]])

M['config.etcd.ssl.verify_peer'] = format_text([[
    Enable verification of the peer's SSL certificate.
]])

M['config.etcd.username'] = 'A username used for authentication.'

M['config.etcd.watchers'] = format_text([[
    Options for watcher requests: watchcreate, watchwait and watchcancel.
]])

M['config.etcd.watchers.reconnect_max_attempts'] = format_text([[
    The maximum number of attempts to reconnect to an etcd server in case
    of connection failure.
]])

M['config.etcd.watchers.reconnect_timeout'] = format_text([[
    The timeout (in seconds) between attempts to reconnect to an etcd
    server in case of connection failure.
]])

-- }}} config.etcd configuration

M['config.reload'] = format_text([[
    Specify how the configuration is reloaded. This option accepts the
    following values:

    - `auto`: configuration is reloaded automatically when it is changed.
    - `manual`: configuration should be reloaded manually. In this case, you can
    reload the configuration in the application code using `config:reload()`.
]])

-- {{{ config.storage configuration

M['config.storage'] = format_text([[
    This section describes options related to providing connection settings
    to a centralized Tarantool-based storage.
]])

M['config.storage.endpoints'] = format_text([[
    An array of endpoints used to access a configuration storage. Each endpoint
    can include the following fields:

    - `uri`: a URI of the configuration storage's instance.
    - `login`: a username used to connect to the instance.
    - `password`: a password used for authentication.
    - `params`: SSL parameters required for encrypted connections
]])

M['config.storage.endpoints.*'] = format_text([[
    Element that represents a configuration storage endpoint with the
    following fields:

    - `uri`: a URI of the configuration storage's instance.
    - `login`: a username used to connect to the instance.
    - `password`: a password used for authentication.
    - `params`: SSL parameters required for encrypted connections.
]])

M['config.storage.endpoints.*.login'] = format_text([[
    A username used to connect to the instance.
]])

M['config.storage.endpoints.*.params'] = M['<uri>.params']

M['config.storage.endpoints.*.params.ssl_ca_file'] =
    M['<uri>.params.ssl_ca_file']

M['config.storage.endpoints.*.params.ssl_cert_file'] =
    M['<uri>.params.ssl_cert_file']

M['config.storage.endpoints.*.params.ssl_ciphers'] =
    M['<uri>.params.ssl_ciphers']

M['config.storage.endpoints.*.params.ssl_key_file'] =
    M['<uri>.params.ssl_key_file']

M['config.storage.endpoints.*.params.ssl_password'] =
    M['<uri>.params.ssl_password']

M['config.storage.endpoints.*.params.ssl_password_file'] =
    M['<uri>.params.ssl_password_file']

M['config.storage.endpoints.*.params.transport'] =  M['<uri>.params.transport']

M['config.storage.endpoints.*.password'] = 'A password used for authentication.'

M['config.storage.endpoints.*.uri'] = format_text([[
    A URI of the configuration storage's instance.
]])

M['config.storage.prefix'] = format_text([[
    A key prefix used to search a configuration in a centralized configuration
    storage. Tarantool searches keys by the following path: `<prefix>/config/*`.
    Note that `<prefix>` should start with a slash (`/`).
]])

M['config.storage.reconnect_after'] = format_text([[
    A number of seconds to wait before reconnecting to a configuration storage.
]])

M['config.storage.timeout'] = format_text([[
    The interval (in seconds) to perform the status check of a configuration
    storage.
]])

-- }}} config.storage configuration

-- }}} config configuration

-- {{{ console configuration

M['console'] = format_text([[
    Configure the administrative console. A client to the console is `tt`
    connect.
]])

M['console.enabled'] = format_text([[
    Whether to listen on the Unix socket provided in the console.socket option.

    If the option is set to `false`, the administrative console is disabled.
]])

M['console.socket'] = format_text([[
    The Unix socket for the administrative console.

    Mind the following nuances:

    1. Only a Unix domain socket is allowed. A TCP socket can't be configured
    this way.

    2.console.socket is a file path, without any `unix:` or `unix/:` prefixes.

    3. If the file path is a relative path, it is interpreted relative
    to `process.work_dir`.
]])

-- }}} console configuration

-- {{{ credentials configuration

M['credentials'] = format_text([[
    The `credentials` section allows you to create users and grant them the
    specified privileges.
]])

-- {{{ credentials.roles configuration

M['credentials.roles'] = format_text([[
    An array of roles that can be granted to users or other roles.
]])

M['credentials.roles.*'] = 'Role name.'

M['credentials.roles.*.privileges'] = format_text([[
    An array of privileges granted to this role.
]])

M['credentials.roles.*.privileges.*'] = format_text([[
    Privileges that can be granted to a user with this role.
]])

M['credentials.roles.*.privileges.*.functions'] = format_text([[
    Functions to which user with this role gets the specified permissions.
]])

M['credentials.roles.*.privileges.*.functions.*'] = 'Function name.'

M['credentials.roles.*.privileges.*.lua_call'] = format_text([[
    Defines the Lua functions that the user with this role has permission to
    call. This field accepts a special value, `all`, which grants the privilege
    to use any global non-built-in Lua functions.
]])

M['credentials.roles.*.privileges.*.lua_call.*'] = 'Lua function name.'

M['credentials.roles.*.privileges.*.lua_eval'] = format_text([[
    Whether this user with this role can execute arbitrary Lua code.
]])

M['credentials.roles.*.privileges.*.permissions'] = format_text([[
    Permissions assigned to user with this role.
]])

M['credentials.roles.*.privileges.*.permissions.*'] = 'Permission name.'

M['credentials.roles.*.privileges.*.sequences'] = format_text([[
    Sequences to which user with this role gets the specified permissions.
]])

M['credentials.roles.*.privileges.*.sequences.*'] = 'Sequence name.'

M['credentials.roles.*.privileges.*.spaces'] = format_text([[
    Spaces to which user with this role gets the specified permissions.
]])

M['credentials.roles.*.privileges.*.spaces.*'] = 'Space name.'

M['credentials.roles.*.privileges.*.sql'] = format_text([[
    Whether user with this role can execute an arbitrary SQL expression.
]])
M['credentials.roles.*.privileges.*.sql.*'] = 'SQL expression name.'

M['credentials.roles.*.privileges.*.universe'] = format_text([[
    Grants global permissions across all object types in the database,
    including:

    - `read`: Read any object
    - `write`: Modify any object
    - `execute`: Execute functions or code
    - `session`: Connect via IPROTO
    - `usage`: Use granted privileges
    - `create`: Create users, roles, objects
    - `drop`: Remove users, roles, objects
    - `alter`: Modify settings or objects
]])

M['credentials.roles.*.roles'] = 'An array of roles granted to this role.'

M['credentials.roles.*.roles.*'] = 'Role name.'

-- }}} credentials.roles configuration

-- {{{ credentials.users configuration

M['credentials.users'] = 'An array of users.'

M['credentials.users.*'] = 'User name.'

M['credentials.users.*.password'] = format_text([[
    A user's password.
]])

M['credentials.users.*.privileges'] = format_text([[
    An array of privileges granted to this user.
]])

M['credentials.users.*.privileges.*'] = format_text([[
    Privileges that can be granted to a user.
]])

M['credentials.users.*.privileges.*.functions'] = format_text([[
    Functions to which this user gets the specified permissions.
]])
M['credentials.users.*.privileges.*.functions.*'] = 'Function name.'

M['credentials.users.*.privileges.*.lua_call'] = format_text([[
    Defines the Lua functions that the user has permission to call. This
    field accepts a special value, `all`, which grants the privilege
    to use any global non-built-in Lua functions.
]])
M['credentials.users.*.privileges.*.lua_call.*'] = 'Lua function name.'

M['credentials.users.*.privileges.*.lua_eval'] = format_text([[
    Whether this user can execute arbitrary Lua code.
]])

M['credentials.users.*.privileges.*.permissions'] = format_text([[
    Permissions assigned to this user or a user with this role.
]])

M['credentials.users.*.privileges.*.permissions.*'] = 'Permission name.'

M['credentials.users.*.privileges.*.sequences'] = format_text([[
    Sequences to which this user gets the specified permissions.
]])

M['credentials.users.*.privileges.*.sequences.*'] = 'Sequence name.'

M['credentials.users.*.privileges.*.spaces'] = format_text([[
    Spaces to which this user gets the specified permissions.
]])

M['credentials.users.*.privileges.*.spaces.*'] = 'Space name.'

M['credentials.users.*.privileges.*.sql'] = format_text([[
    Whether this user can execute an arbitrary SQL expression.
]])

M['credentials.users.*.privileges.*.sql.*'] = 'SQL expression name.'

M['credentials.users.*.privileges.*.universe'] =
    M['credentials.roles.*.privileges.*.universe']

M['credentials.users.*.roles'] = 'An array of roles granted to this user.'

M['credentials.users.*.roles.*'] = 'Role name.'

-- }}} credentials.users configuration

-- }}} credentials configuration

-- {{{ database configuration

M['database'] = format_text([[
    The `database` section defines database-specific configuration parameters,
    such as an instance's read-write mode or transaction isolation level.
]])

M['database.hot_standby'] = format_text([[
    Whether to start the server in the hot standby mode. This mode can be used
    to provide failover without replication.

    Note: `database.hot_standby` has no effect:

    - If `wal.mode` is set to none.
    - If `wal.dir_rescan_delay` is set to a large value on macOS or FreeBSD.
    On these platforms, the hot standby mode is designed so that the loop
    repeats every `wal.dir_rescan_delay` seconds.
    - For spaces created with engine set to `vinyl`.
]])

M['database.instance_uuid'] = format_text([[
    An instance UUID.

    By default, instance UUIDs are generated automatically.
    `database.instance_uuid` can be used to specify an instance identifier
    manually.

    UUIDs should follow these rules:

    - The values must be true unique identifiers, not shared by other instances
    or replica sets within the common infrastructure.
    - The values must be used consistently, not changed after the initial setup.
    The initial values are stored in snapshot files and are checked whenever
    the system is restarted.
    - The values must comply with RFC 4122. The nil UUID is not allowed.
]])

M['database.mode'] = format_text([[
    An instance's operating mode. This option is in effect if
    `replication.failover` is set to `off`.

    The following modes are available:

    - `rw`: an instance is in read-write mode.
    - `ro`: an instance is in read-only mode.

    If not specified explicitly, the default value depends on the number of
    instances in a replica set. For a single instance, the `rw` mode is used,
    while for multiple instances, the `ro` mode is used.
]])

M['database.replicaset_uuid'] = format_text([[
    A replica set UUID.

    By default, replica set UUIDs are generated automatically.
    `database.replicaset_uuid` can be used to specify a replica set identifier
    manually.
]])

M['database.txn_isolation'] = 'A transaction isolation level.'

M['database.txn_timeout'] = format_text([[
    A timeout (in seconds) after which the transaction is rolled back.
]])

M['database.use_mvcc_engine'] = 'Whether the transactional manager is enabled.'

-- }}} database configuration

-- {{{ failover configuration

M['failover'] = format_text([[
    The `failover` section defines parameters related to a supervised failover.
]])

M['failover.call_timeout'] = format_text([[
    A call timeout (in seconds) for monitoring and failover
    requests to an instance.
]])

M['failover.connect_timeout'] = format_text([[
    A connection timeout (in seconds) for monitoring and failover
    requests to an instance.
]])

M['failover.lease_interval'] = format_text([[
    A time interval (in seconds) that specifies how long an instance should
    be a leader without renew requests from a coordinator. When this interval
    expires, the leader switches to read-only mode. This action is performed
    by the instance itself and works even if there is no connectivity between
    the instance and the coordinator.
]])

M['failover.probe_interval'] = format_text([[
    A time interval (in seconds) that specifies how often a monitoring service
    of the failover coordinator polls an instance for its status.
]])

M['failover.renew_interval'] = format_text([[
    A time interval (in seconds) that specifies how often a failover coordinator
    sends read-write deadline renewals.
]])

M['failover.replicasets'] = format_text([[
    Configurate failover priorities for each replicaset. It maps replicaset
    names to instance priorities, where lower priority values are preferred
    during failover. If priorities are equal, instances are selected by
    lexicographical order of their names.
]])

M['failover.replicasets.*'] = 'Name of the replicaset.'

M['failover.replicasets.*.priority'] = format_text([[
    Priorities for the supervised failover mode.
]])

M['failover.replicasets.*.priority.*'] = format_text([[
    The instance's name within this replicaset.
]])

M['failover.stateboard'] = format_text([[
    This options define configuration parameters related to maintaining the
    state of failover coordinators in a remote etcd-based storage.
]])

M['failover.stateboard.keepalive_interval'] = format_text([[
    A time interval (in seconds) that specifies how long a transient state
    information is stored and how quickly a lock expires.

    Note `failover.stateboard.keepalive_interval` should be smaller than
    `failover.lease_interval`. Otherwise, switching of a coordinator causes
    a replica set leader to go to read-only mode for some time.
]])

M['failover.stateboard.renew_interval'] = format_text([[
    A time interval (in seconds) that specifies how often a failover
    coordinator writes its state information to etcd. This option also
    determines the frequency at which an active coordinator reads new
    commands from etcd.
]])

-- }}} failover configuration

-- {{{ feedback configuration

M['feedback'] = format_text([[
    The `feedback` section describes configuration parameters for sending
    information about a running Tarantool instance to the specified feedback
    server.
]])

M['feedback.crashinfo'] = format_text([[
    Whether to send crash information in the case of an instance failure. This
    information includes:

    - General information from the `uname` output.
    - Build information.
    - The crash reason.
    - The stack trace.

    To turn off sending crash information, set this option to `false`.
]])

M['feedback.enabled'] = format_text([[
    Whether to send information about a running instance to the feedback
    server. To turn off sending feedback, set this option to `false`.
]])

M['feedback.host'] = 'The address to which information is sent.'

M['feedback.interval'] = 'The interval (in seconds) of sending information.'

M['feedback.metrics_collect_interval'] = format_text([[
    The interval (in seconds) for collecting metrics.
]])

M['feedback.metrics_limit'] = format_text([[
    The maximum size of memory (in bytes) used to store metrics before
    sending them to the feedback server. If the size of collected metrics
    exceeds this value, earlier metrics are dropped.
]])

M['feedback.send_metrics'] = format_text([[
    Whether to send metrics to the feedback server. Note that all collected
    metrics are dropped after sending them to the feedback server.
]])

-- }}} feedback configuration

-- {{{ fiber configuration

M['fiber'] = format_text([[
    The `fiber` section describes options related to configuring fibers, yields,
    and cooperative multitasking.
]])

M['fiber.io_collect_interval'] = format_text([[
    The time period (in seconds) a fiber sleeps between iterations of the
    event loop.

    `fiber.io_collect_interval` can be used to reduce CPU load in deployments
    where the number of client connections is large, but requests are not so
    frequent (for example, each connection issues just a handful of requests
    per second).
]])

M['fiber.slice'] = format_text([[
    This section describes options related to configuring time periods
    for fiber slices. See `fiber.set_max_slice` for details and examples.
]])

M['fiber.slice.err'] = format_text([[
    Set a time period (in seconds) that specifies the warning slice.
]])

M['fiber.slice.warn'] = format_text([[
    Set a time period (in seconds) that specifies the error slice.
]])

M['fiber.too_long_threshold'] = format_text([[
    If processing a request takes longer than the given period (in seconds),
    the fiber warns about it in the log.

    `fiber.too_long_threshold` has effect only if `log.level` is greater than
    or equal to 4 (`warn`).
]])

M['fiber.top'] = format_text([[
    This section describes options related to configuring the `fiber.top()`
    function, normally used for debug purposes. `fiber.top()` shows all alive
    fibers and their CPU consumption.
]])

M['fiber.top.enabled'] = format_text([[
    Enable or disable the `fiber.top()` function.

    Enabling `fiber.top()` slows down fiber switching by about 15%, so it is
    disabled by default.
]])

M['fiber.worker_pool_threads'] = format_text([[
    The maximum number of threads to use during execution of certain
    internal processes (for example, `socket.getaddrinfo()` and `coio_call()`).
]])

-- }}} fiber configuration

-- {{{ flightrec configuration

M['flightrec'] = format_text([[
    The flightrec section describes options related to the flight recorder
    configuration.
]])

M['flightrec.enabled'] = 'Enable the flight recorder.'

M['flightrec.logs_log_level'] = format_text([[
    Specify the level of detail the log has. The default value is 6
    (`VERBOSE`). You can learn more about log levels from the log_level
    option description. Note that the `flightrec.logs_log_level` value might
    differ from `log_level`.
]])

M['flightrec.logs_max_msg_size'] = format_text([[
    Specify the maximum size (in bytes) of the log message. The log message
    is truncated if its size exceeds this limit.
]])

M['flightrec.logs_size'] = format_text([[
    Specify the size (in bytes) of the log storage. You can set this option
    to 0 to disable the log storage.
]])

M['flightrec.metrics_interval'] = format_text([[
    Specify the time interval (in seconds) that defines the frequency
    of dumping metrics. This value shouldn't exceed `flightrec.metrics_period`.
]])

M['flightrec.metrics_period'] = format_text([[
    Specify the time period (in seconds) that defines how long metrics
    are stored from the moment of dump. So, this value defines how much
    historical metrics data is collected up to the moment of crash. The
    frequency of metric dumps is defined by `flightrec.metrics_interval`.
]])

M['flightrec.requests_max_req_size'] = format_text([[
    Specify the maximum size (in bytes) of a request entry.
    A request entry is truncated if this size is exceeded.
]])

M['flightrec.requests_max_res_size'] = format_text([[
    Specify the maximum size (in bytes) of a response entry.
    A response entry is truncated if this size is exceeded.
]])

M['flightrec.requests_size'] = format_text([[
    Specify the size (in bytes) of storage for the request and
    response data. You can set this parameter to 0 to disable
    a storage of requests and responses.
]])

-- }}} flightrec configuration

-- {{{ iproto configuration

M['iproto'] = format_text([[
    The iproto section is used to configure parameters related to communicating
    to and between cluster instances.
]])

-- {{{ iproto.advertise configuration

M['iproto.advertise'] = format_text([[
    URIs for clients to let them know where to connect.
]])

M['iproto.advertise.client'] = format_text([[
    A URI used to advertise the current instance to clients.

    The iproto.advertise.client option accepts a URI in the following formats:

    - 1. An address: `host:port`.
    - 2. A Unix domain socket: `unix/:`.

    Note that this option doesn't allow to set a username and password.
    If a remote client needs this information, it should be delivered outside
    of the cluster configuration.
]])

M['iproto.advertise.peer'] = format_text([[
    Settings used to advertise the current instance to other cluster members.
    The format of these settings is described in
    `iproto.advertise.<peer_or_sharding>.*`.
]])

M['iproto.advertise.peer.login'] = format_text([[
    (Optional) A username used to connect to the current instance.
    If a username is not set, the guest user is used.
]])

M['iproto.advertise.peer.params'] = M['<uri>.params']

M['iproto.advertise.peer.params.ssl_ca_file'] = M['<uri>.params.ssl_ca_file']

M['iproto.advertise.peer.params.ssl_cert_file'] =
    M['<uri>.params.ssl_cert_file']

M['iproto.advertise.peer.params.ssl_ciphers'] = M['<uri>.params.ssl_ciphers']

M['iproto.advertise.peer.params.ssl_key_file'] = M['<uri>.params.ssl_key_file']

M['iproto.advertise.peer.params.ssl_password'] = M['<uri>.params.ssl_password']

M['iproto.advertise.peer.params.ssl_password_file'] =
    M['<uri>.params.ssl_password_file']

M['iproto.advertise.peer.params.transport'] = M['<uri>.params.transport']

M['iproto.advertise.peer.password'] = format_text([[
    (Optional) A password for the specified user. If a login is specified but
    a password is missing, it is taken from the user's credentials.
]])

M['iproto.advertise.peer.uri'] = format_text([[
    (Optional) A URI used to advertise the current instance. By default,
    the URI defined in iproto.listen is used to advertise the current instance.
]])

M['iproto.advertise.sharding'] = format_text([[
    Settings used to advertise the current instance to a router and
    rebalancer. The format of these settings is described in
    `iproto.advertise.<peer_or_sharding>.*`.
]])

M['iproto.advertise.sharding.login'] = format_text([[
    (Optional) A username used to connect to the current instance.
    If a username is not set, the guest user is used.
]])

M['iproto.advertise.sharding.params'] = M['<uri>.params']

M['iproto.advertise.sharding.params.ssl_ca_file'] =
    M['<uri>.params.ssl_ca_file']

M['iproto.advertise.sharding.params.ssl_cert_file'] =
    M['<uri>.params.ssl_cert_file']

M['iproto.advertise.sharding.params.ssl_ciphers'] =
    M['<uri>.params.ssl_ciphers']

M['iproto.advertise.sharding.params.ssl_key_file'] =
    M['<uri>.params.ssl_key_file']

M['iproto.advertise.sharding.params.ssl_password'] =
    M['<uri>.params.ssl_password']

M['iproto.advertise.sharding.params.ssl_password_file'] =
    M['<uri>.params.ssl_password_file']

M['iproto.advertise.sharding.params.transport'] = M['<uri>.params.transport']

M['iproto.advertise.sharding.password'] = format_text([[
    (Optional) A password for the specified user. If a login is specified but
    a password is missing, it is taken from the user's credentials.
]])

M['iproto.advertise.sharding.uri'] = format_text([[
    (Optional) A URI used to advertise the current instance. By default,
    the URI defined in iproto.listen is used to advertise the current instance.
]])

-- }}} iproto.advertise configuration

M['iproto.listen'] = format_text([[
    An array of URIs used to listen for incoming requests. If required,
    you can enable SSL for specific URIs by providing additional parameters
    (`<uri>.params.*`).

    Note that a URI value can't contain parameters, a login, or a password.
]])

M['iproto.listen.*'] = format_text([[
    Uri params. For example `- uri: '127.0.0.1:3301'`.

    Note that a URI value can't contain parameters, a login, or a password.
]])

M['iproto.listen.*.params'] =
    M['<uri>.params']

M['iproto.listen.*.params.ssl_ca_file'] =
    M['<uri>.params.ssl_ca_file']

M['iproto.listen.*.params.ssl_cert_file'] =
    M['<uri>.params.ssl_cert_file']

M['iproto.listen.*.params.ssl_ciphers'] =
    M['<uri>.params.ssl_ciphers']

M['iproto.listen.*.params.ssl_key_file'] =
    M['<uri>.params.ssl_key_file']

M['iproto.listen.*.params.ssl_password'] =
    M['<uri>.params.ssl_password']

M['iproto.listen.*.params.ssl_password_file'] =
    M['<uri>.params.ssl_password_file']

M['iproto.listen.*.params.transport'] =
    M['config.storage.endpoints.*.params.transport']

M['iproto.listen.*.uri'] = M['iproto.listen.*']

M['iproto.net_msg_max'] = format_text([[
    To handle messages, Tarantool allocates fibers. To prevent fiber overhead
    from affecting the whole system, Tarantool restricts how many messages the
    fibers handle, so that some pending requests are blocked.

    - On powerful systems, increase `net_msg_max`, and the scheduler starts
    processing pending requests immediately.
    - On weaker systems, decrease `net_msg_max`, and the overhead may decrease.
    However, this may take some time because the scheduler must wait until
    already-running requests finish.

    When `net_msg_max` is reached, Tarantool suspends processing of incoming
    packages until it has processed earlier messages. This is not a direct
    restriction of the number of fibers that handle network messages, rather
    it is a system-wide restriction of channel bandwidth. This in turn restricts
    the number of incoming network messages that the transaction processor
    thread handles, and therefore indirectly affects the fibers that handle
    network messages.
]])

M['iproto.readahead'] = format_text([[
    The size of the read-ahead buffer associated with a client connection. The
    larger the buffer, the more memory an active connection consumes, and the
    more requests can be read from the operating system buffer in a single
    system call.

    The recommendation is to make sure that the buffer can contain at least a
    few dozen requests. Therefore, if a typical tuple in a request is large,
    e.g. a few kilobytes or even megabytes, the read-ahead buffer size should
    be increased. If batched request processing is not used, it's prudent to
    leave this setting at its default.
]])

M['iproto.threads'] = format_text([[
    The number of network threads. There can be unusual workloads where the
    network thread is 100% loaded and the transaction processor thread is not,
    so the network thread is a bottleneck. In that case, set `iproto_threads`
    to 2 or more. The operating system kernel determines which connection goes
    to which thread.
]])

-- }}} iproto configuration

-- {{{ groups configuration

M['groups'] = format_text([[
    This section provides the ability to define the full topology
    of a Tarantool cluster.
]])

M['groups.*'] = format_text([[
    A group name.

    The following rules are applied to group names:

    - The maximum number of symbols is 63.
    - Should start with a letter.
    - Can contain lowercase letters (a-z).
    - Can contain digits (0-9).
    - Can contain the following characters: -, _.
]])

M['groups.*.replicasets'] = 'Replica sets that belong to this group.'

M['groups.*.replicasets.*'] = format_text([[
    A replica set name.

    Note that the rules applied to a replica set name are the same as for
    groups. Learn more in `groups.<group_name>`.
]])

M['groups.*.replicasets.*.instances'] = format_text([[
    Instances that belong to this replica set.
]])

M['groups.*.replicasets.*.instances.*'] = 'An instance name.'

M['groups.*.replicasets.*.leader'] = format_text([[
    A replica set leader. This option can be used to set a replica set leader
    when manual `replication.failover` is used.

    To perform controlled failover, `<replicaset_name>.leader` can be
    temporarily removed or set to null.
]])

M['groups.*.replicasets.*.bootstrap_leader'] = format_text([[
    A bootstrap leader for a replica set. To specify a bootstrap leader
    manually, you need to set `replication.bootstrap_strategy` to `config`.
]])

-- }}} groups configuration

-- {{{ labels configuration

M['labels'] = format_text([[
    The `labels` section allows adding custom attributes to the configuration.
    Attributes must be key: value pairs with string keys and values.
]])

M['labels.*'] = 'A value of the label with the specified name.'

-- }}} labels configuration

-- {{{ log configuration

M['log'] = format_text([[
    The `log` section defines configuration parameters related to logging.
    To handle logging in your application, use the log module.
]])

M['log.file'] = format_text([[
    Specify a file for logs destination. To write logs to a file, you need
    to set `log.to` to file. Otherwise, `log.file` is ignored.
]])

M['log.format'] = format_text([[
    Specify a format that is used for a log entry. The following formats
    are supported:

    - `plain`: a log entry is formatted as plain text.
    - `json`: a log entry is formatted as JSON and includes additional fields.
]])

M['log.level'] = format_text([[
    Specify the level of detail logs have. There are the following levels:

    - 0: `fatal`
    - 1: `syserror`
    - 2: `error`
    - 3: `crit`
    - 4: `warn`
    - 5: `info`
    - 6: `verbose`
    - 7: `debug`

    By setting log.level, you can enable logging of all events with
    severities above or equal to the given level.
]])

M['log.modules'] = format_text([[
    Configure the specified log levels (`log.level`) for different modules.

    You can specify a logging level for the following module types:

    - Modules (files) that use the default logger. Example: Set log levels for
    files that use the default logger.
    - Modules that use custom loggers created using the `log.new()` function.
    Example: Set log levels for modules that use custom loggers.
    - The tarantool module that enables you to configure the logging level
    for Tarantool core messages. Specifically, it configures the logging level
    for messages logged from non-Lua code, including C modules. Example: Set
    a log level for C modules.
]])

M['log.modules.*'] = format_text([[
    Path to module.

    For example: you have module placed by the following path:
    `test/module.lua.` To configure logging levels, you need to
    provide module names corresponding to paths to these modules:
    ``` test.module: 'verbose' ```.
]])

M['log.nonblock'] = format_text([[
    Specify the logging behavior if the system is not ready to write.
    If set to `true`, Tarantool does not block during logging if the system
    is non-writable and writes a message instead. Using this value may
    improve logging performance at the cost of losing some log messages.
]])

M['log.pipe'] = format_text([[
    Start a program and write logs to its standard input (`stdin`). To send
    logs to a program's standard input, you need to set `log.to` to `pipe`.
]])

M['log.syslog'] = 'Syslog configurations params.'

M['log.syslog.facility'] = format_text([[
    Specify the syslog facility to be used when syslog is enabled.
    To write logs to syslog, you need to set `log.to` to `syslog`.
]])

M['log.syslog.identity'] = format_text([[
    Specify an application name used to identify Tarantool messages in
    syslog logs. To write logs to syslog, you need to set `log.to` to `syslog`.
]])

M['log.syslog.server'] = format_text([[
    Set a location of a syslog server. This option accepts one of the following
    values:

    - An IPv4 address. Example: `127.0.0.1:514`.
    - A Unix socket path starting with unix:. Examples:
    `unix:/dev/log` on Linux or `unix:/var/run/syslog` on macOS.

    To write logs to syslog, you need to set log.to to syslog.
]])

M['log.to'] = format_text([[
    Define a location Tarantool sends logs to. This option accepts the
    following values:

    - `stderr`: write logs to the standard error stream.
    - `file`: write logs to a file (see `log.file`).
    - `pipe`: start a program and write logs to its standard input
    (see `log.pipe`).
    - `syslog`: write logs to a system logger (see `log.syslog.*`).
]])

-- }}} log configuration

-- {{{ memtx configuration

M['memtx'] = format_text([[
   This section is used to configure parameters related to the memtx engine.
]])

M['memtx.allocator'] = format_text([[
    Specify the allocator that manages memory for memtx tuples. Possible values:

    - `system` - the memory is allocated as needed, checking that the quota
    is not exceeded. THe allocator is based on the `malloc` function.
    - `small` - a slab allocator. The allocator repeatedly uses a memory
    block to allocate objects of the same type. Note that this allocator is
    prone to unresolvable fragmentation on specific workloads, so you can
    switch to `system` in such cases.
]])

M['memtx.max_tuple_size'] = format_text([[
    Size of the largest allocation unit for the memtx storage engine in bytes.
    It can be increased if it is necessary to store large tuples.
]])

M['memtx.memory'] = format_text([[
    The amount of memory in bytes that Tarantool allocates to store tuples.
    When the limit is reached, `INSERT` and `UPDATE` requests fail with the
    `ER_MEMORY_ISSUE` error. The server does not go beyond the `memtx.memory`
    limit to allocate tuples, but there is additional memory used to store
    indexes and connection information.
]])

M['memtx.min_tuple_size'] = format_text([[
    Size of the smallest allocation unit in bytes. It can be decreased if
    most of the tuples are very small.
]])

M['memtx.slab_alloc_factor'] = format_text([[
    The multiplier for computing the sizes of memory chunks that tuples
    are stored in. A lower value may result in less wasted memory depending
    on the total amount of memory available and the distribution of item sizes.
]])

M['memtx.slab_alloc_granularity'] = format_text([[
    Specify the granularity in bytes of memory allocation in the small
    allocator. The `memtx.slab_alloc_granularity` value should meet the
    following conditions:

    - The value is a power of two.
    - The value is greater than or equal to 4.

    Below are few recommendations on how to adjust the
    `memtx.slab_alloc_granularity option`:

    - If the tuples in space are small and have about the same size, set the
    option to 4 bytes to save memory.
    - If the tuples are different-sized, increase the option value to allocate
    tuples from the same `mempool` (memory pool).
]])

M['memtx.sort_threads'] = format_text([[
    The number of threads from the thread pool used to sort keys of secondary
    indexes on loading a `memtx` database. The minimum value is 1, the maximum
    value is 256. The default is to use all available cores.
]])

-- }}} memtx configuration

-- {{{ metrics configuration

M['metrics'] = format_text([[
    The `metrics` section defines configuration parameters for metrics.
]])

M['metrics.exclude'] = format_text([[
    An array containing the metrics to turn off. The array can contain the same
    values as the `exclude` configuration parameter passed to `metrics.cfg()`.
]])

M['metrics.exclude.*'] = 'Metrica name.'

M['metrics.include'] = format_text([[
    An array containing the metrics to turn on. The array can contain the same
    values as the `include` configuration parameter passed to `metrics.cfg()`.
]])

M['metrics.include.*'] = 'Metrica name.'

M['metrics.labels'] = 'Global labels to be added to every observation.'

M['metrics.labels.*'] = 'Label name.'

-- }}} metrics configuration

-- {{{ process configuration

M['process'] = format_text([[
    The `process` section defines configuration parameters of the Tarantool
    process in the system.
]])

M['process.background'] = format_text([[
    Run the server as a daemon process.

    If this option is set to true, Tarantool log location defined by the
    `log.to` option should be set to file, pipe, or syslog - anything other
    than stderr, the default, because a daemon process is detached from a
    terminal and it can't write to the terminal's stderr. Warn: Do not enable
    the background mode for applications intended to run by the tt utility.
]])

M['process.coredump'] = format_text([[
    Create coredump files.

    Usually, an administrator needs to call `ulimit -c unlimited` (or set
    corresponding options in systemd's unit file) before running a Tarantool
    process to get core dumps. If `process.coredump` is enabled, Tarantool sets
    the corresponding resource limit by itself and the administrator doesn't
    need to call `ulimit -c unlimited` (see man 3 setrlimit).

    This option also sets the state of the `dumpable` attribute, which is
    enabled by default, but may be dropped in some circumstances (according
    to man 2 prctl, see PR_SET_DUMPABLE).
]])

M['process.pid_file'] = format_text([[
    Store the process id in this file.

    This option may contain a relative file path. In this case, it is
    interpreted as relative to `process.work_dir`.
]])

M['process.strip_core'] = format_text([[
    Whether coredump files should not include memory allocated for tuples -
    this memory can be large if Tarantool runs under heavy load. Setting to
    `true` means "do not include".
]])

M['process.title'] = format_text([[
    Add the given string to the server's process title (it is shown in the
    COMMAND column for the Linux commands `ps -ef` and `top -c`).
]])

M['process.username'] = 'The name of the system user to switch to after start.'

M['process.work_dir'] = format_text([[
    A directory where Tarantool working files will be stored (database files,
    logs, a PID file, a console Unix socket, and other files if an application
    generates them in the current directory). The server instance switches to
    `process.work_dir` with chdir(2) after start.

    If set as a relative file path, it is relative to the current working
    directory, from where Tarantool is started. If not specified, defaults
    to the current working directory.

    Other directory and file parameters, if set as relative paths, are
    interpreted as relative to `process.work_dir`, for example, directories
    for storing snapshots and write-ahead logs.
]])

-- }}} process configuration

-- {{{ replication configuration

M['replication'] = format_text([[
    This section defines configuration parameters related to replication.
]])

M['replication.anon'] = format_text([[
    Whether to make the current instance act as an anonymous replica. Anonymous
    replicas are read-only and can be used, for example, for backups.

    To make the specified instance act as an anonymous replica, set
    `replication.anon` to `true`.

    Anonymous replicas are not displayed in the `box.info.replication` section.
    You can check their status using `box.info.replication_anon()`.

    While anonymous replicas are read-only, you can write data to
    replication-local and temporary spaces (created with `is_local = true`
    and `temporary = true`, respectively). Given that changes to
    replication-local spaces are allowed, an anonymous replica might
    increase the 0 component of the vclock value.

    Here are the limitations of having anonymous replicas in a replica set:

    - A replica set must contain at least one non-anonymous instance.
    - An anonymous replica can't be configured as a writable instance by
    setting database.mode to rw or making it a leader using
    `<replicaset_name>.leader.`
    - If `replication.failover` is set to election, an anonymous replica can
    have `replication.election_mode` set to `off` only.
    - If `replication.failover` is set to `supervised`, an external failover
    coordinator doesn't consider anonymous replicas when selecting a bootstrap
    or replica set leader.
]])

M['replication.bootstrap_strategy'] = format_text([[
    Specifies a strategy used to bootstrap a replica set. The following
    strategies are available:

    - `auto`: a node doesn't boot if half or more of the other nodes in a
    replica set are not connected. For example, if a replica set contains 2
    or 3 nodes, a node requires 2 connected instances. In the case of 4 or 5
    nodes, at least 3 connected instances are required. Moreover, a bootstrap
    leader fails to boot unless every connected node has chosen it as a
    bootstrap leader.
    - `config`: use the specified node to bootstrap a replica set. To specify
    the bootstrap leader, use the `<replicaset_name>.bootstrap_leader` option.
    - `supervised`: a bootstrap leader isn't chosen automatically but should
    be appointed using `box.ctl.make_bootstrap_leader()` on the desired node.
    - `legacy` (deprecated since 2.11.0): a node requires the
    `replication_connect_quorum` number of other nodes to be connected. This
    option is added to keep the compatibility with the current versions of
    Cartridge and might be removed in the future.
]])

M['replication.connect_timeout'] = format_text([[
    A timeout (in seconds) a replica waits when trying to connect to a master
    in a cluster. See orphan status for details.

    This parameter is different from replication.timeout, which a master uses
    to disconnect a replica when the master receives no acknowledgments of
    heartbeat messages.
]])

M['replication.election_fencing_mode'] = format_text([[
    Specifies the leader fencing mode that affects the leader election process.
    When the parameter is set to soft or strict, the leader resigns its
    leadership if it has less than replication.synchro_quorum of alive
    connections to the cluster nodes. The resigning leader receives the status
    of a follower in the current election term and becomes read-only.

    - In `soft` mode, a connection is considered dead if there are no responses
    for 4 * `replication.timeout` seconds both on the current leader and the
    followers.
    - In `strict` mode, a connection is considered dead if there are no
    responses for 2 * `replication.timeout` seconds on the current leader
    and 4 * `replication.timeout` seconds on the followers. This improves the
    chances that there is only one leader at any time.

    Fencing applies to the instances that have the `replication.election_mode`
    set to `candidate` or `manual`. To turn off leader fencing, set
    `election_fencing_mode` to off.
]])

M['replication.election_mode'] = format_text([[
    A role of a replica set node in the leader election process.

    The possible values are:

    - `off`: a node doesn't participate in the election activities.
    - `voter`: a node can participate in the election process but can't be
    a leader.
    - `candidate`: a node should be able to become a leader.
    - `manual`: allow to control which instance is the leader explicitly
    instead of relying on automated leader election. By default, the instance
    acts like a voter - it is read-only and may vote for other candidate
    instances. Once `box.ctl.promote()` is called, the instance becomes a
    candidate and starts a new election round. If the instance wins the
    elections, it becomes a leader but won't participate in any new elections.
]])

M['replication.election_timeout'] = format_text([[
    Specifies the timeout (in seconds) between election rounds in the leader
    election process if the previous round ended up with a split vote.

    It is quite big, and for most of the cases, it can be lowered to 300-400 ms.

    To avoid the split vote repeat, the timeout is randomized on each node
    during every new election, from 100% to 110% of the original timeout value.
    For example, if the timeout is 300 ms and there are 3 nodes started the
    election simultaneously in the same term, they can set their election
    timeouts to 300, 310, and 320 respectively, or to 305, 302, and 324, and
    so on. In that way, the votes will never be split because the election on
    different nodes won't be restarted simultaneously.
]])

M['replication.failover'] = format_text([[
    A failover mode used to take over a master role when the current master
    instance fails. The following modes are available:

    - `off`: Leadership in a replica set is controlled using the
    `database.mode` option. In this case, you can set the `database.mode` option
    to rw on all instances in a replica set to make a master-master
    configuration.

    The default database.mode is determined as follows: `rw` if there is one
    instance in a replica set; `ro` if there are several instances.

    - `manual`: Leadership in a replica set is controlled using the
    `<replicaset_name>.leader` option. In this case, a master-master
    configuration is forbidden.

    In the`manual` mode, the `database.mode` option cannot be set explicitly.
    The leader is configured in the read-write mode, all the other instances
    are read-only.

    - `election`: Automated leader election is used to control leadership in a
    replica set. In the election mode, `database.mode` and
    `<replicaset_name>.leader` shouldn't be set explicitly.
    - `supervised`: (Enterprise Edition only) Leadership in a replica set is
    controlled using an external failover coordinator.

    In the `supervised` mode, `database.mode` and `<replicaset_name>.leader`
    shouldn't be set explicitly.
]])

M['replication.peers'] = format_text([[
    URIs of instances that constitute a replica set. These URIs are used by
    an instance to connect to another instance as a replica.

    Alternatively, you can use iproto.advertise.peer to specify a URI used to
    advertise the current instance to other cluster members.
]])

M['replication.peers.*'] = format_text([[
    specifies URIs of replica set instances.

    For example: `- replicator:topsecret@127.0.0.1:3301`.
]])

M['replication.skip_conflict'] = format_text([[
    By default, if a replica adds a unique key that another replica has added,
    replication stops with the `ER_TUPLE_FOUND` error. If
    `replication.skip_conflict` is set to `true`, such errors are ignored.
]])

M['replication.sync_lag'] = format_text([[
    The maximum delay (in seconds) between the time when data is written to
    the master and the time when it is written to a replica. If
    `replication.sync_lag` is set to `nil` or `365 * 100 * 86400`
    (`TIMEOUT_INFINITY`), a replica is always considered to be "synced".
]])

M['replication.sync_timeout'] = format_text([[
    The timeout (in seconds) that a node waits when trying to sync with other
    nodes in a replica set after connecting or during a configuration update.
    This could fail indefinitely if `replication.sync_lag` is smaller than
    network latency, or if the replica cannot keep pace with master updates.
    If `replication.sync_timeout` expires, the replica enters `orphan` status.
]])

M['replication.synchro_queue_max_size'] = format_text([[
    Puts a limit on the number of transactions in the master synchronous queue.

    `replication.synchro_queue_max_size` is measured in number of bytes to
    be written (0 means unlimited, which was the default behaviour before).
    This option affects only the behavior of the master, and defaults to
    16 megabytes.

    Now that `replication.synchro_queue_max_size` is set on the master node,
    tarantool will discard new transactions that try to queue after the limit
    is reached. If a transaction had to be discarded, user will get an error
    message "The synchronous transaction queue is full".

    This limitation does not apply during the recovery process.
]])

M['replication.synchro_quorum'] = format_text([[
    A number of replicas that should confirm the receipt of a synchronous
    transaction before it can finish its commit.

    This option supports dynamic evaluation of the quorum number. For example,
    the default value is `N / 2 + 1` where `N` is the current number of replicas
    registered in a cluster. Once any replicas are added or removed, the
    expression is re-evaluated automatically.

    Note that the default value (`at least 50% of the cluster size + 1`)
    guarantees data reliability. Using a value less than the canonical one
    might lead to unexpected results, including a split-brain.

    `replication.synchro_quorum` is not used on replicas. If the master fails,
    the pending synchronous transactions will be kept waiting on the replicas
    until a new master is elected.
]])

M['replication.synchro_timeout'] = format_text([[
    For synchronous replication only. Specify how many seconds to wait for a
    synchronous transaction quorum replication until it is declared failed and
    is rolled back.

    It is not used on replicas, so if the master fails, the pending synchronous
    transactions will be kept waiting on the replicas until a new master is
    elected.
]])

M['replication.threads'] = format_text([[
    The number of threads spawned to decode the incoming replication data.

    In most cases, one thread is enough for all incoming data. Possible values
    range from 1 to 1000. If there are multiple replication threads, connections
    to serve are distributed evenly between the threads.
]])

M['replication.timeout'] = format_text([[
    A time interval (in seconds) used by a master to send heartbeat requests to
    a replica when there are no updates to send to this replica. For each
    request, a replica should return a heartbeat acknowledgment.

    If a master or replica gets no heartbeat message for
    `4 * replication.timeout` seconds, a connection is dropped and a replica
    tries to reconnect to the master.
]])

-- }}} replication configuration

-- {{{ roles configuration

M['roles'] = format_text([[
    Specify the roles of an instance. To specify a role's configuration,
    use the roles_cfg option.
]])

M['roles.*'] = 'Role name.'

-- }}} roles configuration

-- {{{ roles_cfg configuration

M['roles_cfg'] = format_text([[
    Specify a role's configuration. This option accepts a role name as the
    key and a role's configuration as the value. To specify the roles of an
    instance, use the roles option.
]])

M['roles_cfg.*'] = 'Role name.'

-- }}} roles_cfg configuration

-- {{{ security configuration

M['security'] = format_text([[
    This section defines configuration parameters related to various
    security settings.
]])

M['security.auth_delay'] = format_text([[
    Specify a period of time (in seconds) that a specific user should wait for
    the next attempt after failed authentication.

    The `security.auth_retries` option lets a client try to authenticate the
    specified number of times before `security.auth_delay` is enforced.

    In the configuration below, Tarantool lets a client try to authenticate
    with the same username three times. At the fourth attempt, the
    authentication delay configured with `security.auth_delay` is enforced.
    This means that a client should wait 10 seconds after the first failed
    attempt.
]])

M['security.auth_retries'] = format_text([[
    Specify the maximum number of authentication retries allowed before
    `security.auth_delay` is enforced. The default value is 0, which means
    `security.auth_delay` is enforced after the first failed authentication
    attempt.

    The retry counter is reset after `security.auth_delay` seconds since the
    first failed attempt. For example, if a client tries to authenticate fewer
    than `security.auth_retries` times within `security.auth_delay` seconds,
    no authentication delay is enforced. The retry counter is also reset after
    any successful authentication attempt.
]])

M['security.auth_type'] = format_text([[
    Specify a protocol used to authenticate users. The possible values are:

    - `chap-sha1`: use the CHAP protocol with SHA-1 hashing applied to
    passwords.
    - `pap-sha256`: use PAP authentication with the SHA256 hashing algorithm.

    Note that CHAP stores password hashes in the `_user` space unsalted. If an
    attacker gains access to the database, they may crack a password, for
    example, using a rainbow table. For PAP, a password is salted with a
    user-unique salt before saving it in the database, which keeps the
    database protected from cracking using a rainbow table.
]])

M['security.disable_guest'] = format_text([[
    If `true`, turn off access over remote connections from unauthenticated or
    guest users. This option affects connections between cluster members and
    `net.box` connections.
]])

M['security.password_enforce_digits'] = format_text([[
    If true, a password should contain digits (0-9).
]])

M['security.password_enforce_lowercase'] = format_text([[
    If true, a password should contain lowercase letters (a-z).
]])

M['security.password_enforce_specialchars'] = format_text([[
    If true, a password should contain at least one special character
    (such as &|?!@$).
]])

M['security.password_enforce_uppercase'] = format_text([[
    If true, a password should contain uppercase letters (A-Z).
]])

M['security.password_history_length'] = format_text([[
    Specify the number of unique new user passwords before an old password
    can be reused. Note tarantool uses the auth_history field in the
    `box.space._user` system space to store user passwords.
]])

M['security.password_lifetime_days'] = format_text([[
    Specify the maximum period of time (in days) a user can use the
    same password. When this period ends, a user gets the "Password expired"
    error on a login attempt. To restore access for such users,
    `use box.schema.user.passwd`.
]])

M['security.password_min_length'] = format_text([[
    Specify the minimum number of characters for a password.
]])

M['security.secure_erasing'] = format_text([[
    If `true`, forces Tarantool to overwrite a data file a few times before
    deletion to render recovery of a deleted file impossible. The option
    applies to both `.xlog` and `.snap` files as well as Vinyl data files.
]])

-- }}} security configuration

-- {{{ sharding configuration

M['sharding'] = format_text([[
    This section defines configuration parameters related to sharding.
]])

M['sharding.bucket_count'] = format_text([[
    The total number of buckets in a cluster. Learn more in Bucket count.
]])

M['sharding.connection_outdate_delay'] = format_text([[
    Time to outdate old objects on reload.
]])

M['sharding.discovery_mode'] = format_text([[
    A mode of the background discovery fiber used by the router to find buckets.
]])

M['sharding.failover_ping_timeout'] = format_text([[
    The timeout (in seconds) after which a node is considered unavailable if
    there are no responses during this period. The failover fiber is used to
    detect if a node is down.
]])

M['sharding.lock'] = format_text([[
    Whether a replica set is locked. A locked replica set cannot receive new
    buckets nor migrate its own buckets.
]])

M['sharding.rebalancer_disbalance_threshold'] = format_text([[
    The maximum bucket disbalance threshold (in percent). The disbalance
    is calculated for each replica set using the following formula:

    `|etalon_bucket_count - real_bucket_count| / etalon_bucket_count * 100`
]])

M['sharding.rebalancer_max_receiving'] = format_text([[
    The maximum number of buckets that can be received in parallel by a single
    replica set. This number must be limited because the rebalancer sends a
    large number of buckets from the existing replica sets to the newly added
    one. This produces a heavy load on the new replica set.
]])

M['sharding.rebalancer_max_sending'] = format_text([[
    The degree of parallelism for parallel rebalancing.
]])

M['sharding.rebalancer_mode'] = format_text([[
    Configure how a rebalancer is selected:

    -`auto` (default): if there are no replica sets with the rebalancer
    sharding role (`sharding.roles`), a replica set with the rebalancer is
    selected automatically among all replica sets.
    - `manual`: one of the replica sets should have the rebalancer sharding
    role. The rebalancer is in this replica set.
    - `off`: rebalancing is turned off regardless of whether a replica set
    with the rebalancer sharding role exists or not.
]])

M['sharding.roles'] = format_text([[
    Roles of a replica set in regard to sharding. A replica set can have the
    following roles:

    - `router`: a replica set acts as a router.
    - `storage`: a replica set acts as a storage.
    - `rebalancer`: a replica set acts as a rebalancer.

    The rebalancer role is optional. If it is not specified, a rebalancer is
    selected automatically from the master instances of replica sets.

    There can be at most one replica set with the rebalancer role.
    Additionally, this replica set should have a `storage` role.
]])

M['sharding.roles.*'] = 'Role name.'

M['sharding.sched_move_quota'] = format_text([[
    A scheduler's bucket move quota used by the rebalancer.

    `sched_move_quota` defines how many bucket moves can be done in a row if
    there are pending storage refs. Then, bucket moves are blocked and a router
    continues making map-reduce requests.
]])

M['sharding.sched_ref_quota'] = format_text([[
    A scheduler's storage ref quota used by a router's map-reduce API.
    For example, the `vshard.router.map_callrw()` function implements
    consistent map-reduce over the entire cluster.

    `sched_ref_quota` defines how many storage refs, therefore map-reduce
    requests, can be executed on the storage in a row if there are pending
    bucket moves. Then, storage refs are blocked and the rebalancer continues
    bucket moves.
]])

M['sharding.shard_index'] = format_text([[
    The name or ID of a TREE index over the bucket id. Spaces without this
    index do not participate in a sharded Tarantool cluster and can be used
    as regular spaces if needed. It is necessary to specify the first part of
    the index, other parts are optional.
]])

M['sharding.sync_timeout'] = format_text([[
    The timeout to wait for synchronization of the old master with replicas
    before demotion. Used when switching a master or when manually calling
    the `sync()` function.
]])

M['sharding.weight'] = format_text([[
    The relative amount of data that a replica set can store.
]])

M['sharding.zone'] = format_text([[
    A zone that can be set for routers and replicas. This allows sending
    read-only requests not only to a master instance but to any available
    replica that is the nearest to the router.
]])

-- }}} sharding configuration

-- {{{ snapshot configuration

M['snapshot'] = format_text([[
    This section defines configuration parameters related to the
    snapshot files. To learn more about the snapshots configuration,
    check the Persistence page.
]])

M['snapshot.by'] = format_text([[
    An object containing configuration options that specify the conditions
    under which automatic snapshots are created by the checkpoint daemon.
    This includes settings like `interval` for time-based snapshots and
    `wal_size` for snapshots triggered when the total size of WAL files
    exceeds a certain threshold.
]])

M['snapshot.by.interval'] = format_text([[
    The interval in seconds between actions by the checkpoint daemon. If
    the option is set to a value greater than zero, and there is activity
    that causes change to a database, then the checkpoint daemon calls
    `box.snapshot()` every `snapshot.by.interval` seconds, creating a new
    snapshot file each time. If the option is set to zero, the checkpoint
    daemon is disabled.
]])

M['snapshot.by.wal_size'] = format_text([[
    The threshold for the total size in bytes for all WAL files created
    since the last snapshot taken. Once the configured threshold is exceeded,
    the WAL thread notifies the checkpoint daemon that it must make a new
    snapshot and delete old WAL files.
]])

M['snapshot.count'] = format_text([[
    The maximum number of snapshots that are stored in the `snapshot.dir`
    directory. If the number of snapshots after creating a new one exceeds
    this value, the Tarantool garbage collector deletes old snapshots.
    If `snapshot.count` is set to zero, the garbage collector does not delete
    old snapshots.
]])

M['snapshot.dir'] = format_text([[
    A directory where memtx stores snapshot (`.snap`) files. A relative path
    in this option is interpreted as relative to `process.work_dir`.

    By default, snapshots and WAL files are stored in the same directory.
    However, you can set different values for the `snapshot.dir` and `wal.dir`
    options to store them on different physical disks for performance matters.
]])

M['snapshot.snap_io_rate_limit'] = format_text([[
    Reduce the throttling effect of `box.snapshot()` on `INSERT/UPDATE/DELETE`
    performance by setting a limit on how many megabytes per second it can
    write to disk. The same can be achieved by splitting `wal.dir` and
    `snapshot.dir` locations and moving snapshots to a separate disk. The
    limit also affects what `box.stat.vinyl().regulator` may show for the write
    rate of dumps to `.run` and `.index` files.
]])

-- }}} snapshot configuration

-- {{{ sql configuration

M['sql'] = 'This section defines configuration parameters related to SQL.'

M['sql.cache_size'] = format_text([[
    The maximum cache size (in bytes) for all SQL prepared statements.
    To see the actual cache size, use `box.info.sql().cache.size`.
]])

-- }}} sql configuration

-- {{{ vinyl configuration

M['vinyl'] = format_text([[
   This section defines configuration parameters related to
   the vinyl storage engine.
]])

M['vinyl.bloom_fpr'] = format_text([[
    A bloom filter's false positive rate - the suitable probability of the
    bloom filter to give a wrong result. The `vinyl.bloom_fpr` setting is a
    default value for the bloom_fpr option passed to
    `space_object:create_index()`.
]])

M['vinyl.cache'] = format_text([[
    The cache size for the vinyl storage engine. The cache can
    be resized dynamically.
]])

M['vinyl.defer_deletes'] = format_text([[
    Enable the deferred DELETE optimization in vinyl. It was disabled by
    default since Tarantool version 2.10 to avoid possible performance
    degradation of secondary index reads.
]])

M['vinyl.dir'] = format_text([[
    A directory where vinyl files or subdirectories will be stored. This option
    may contain a relative file path. In this case, it is interpreted as
    relative to `process.work_dir`.
]])

M['vinyl.max_tuple_size'] = format_text([[
    The size of the largest allocation unit, for the vinyl storage engine.
    It can be increased if it is necessary to store large tuples.
]])

M['vinyl.memory'] = format_text([[
    The maximum number of in-memory bytes that vinyl uses.
]])

M['vinyl.page_size'] = format_text([[
    The page size. A page is a read/write unit for vinyl disk operations.
    The `vinyl.page_size` setting is a default value for the page_size option
    passed to `space_object:create_index()`.
]])

M['vinyl.range_size'] = format_text([[
    The default maximum range size for a vinyl index, in bytes. The maximum
    range size affects the decision of whether to split a range.

    If `vinyl.range_size` is specified (but the value is not null or 0), then it
    is used as the default value for the range_size option passed to
    `space_object:create_index()`.

    If `vinyl.range_size` is not specified (or is explicitly set to null or 0),
    and `range_size` is not specified when the index is created, then Tarantool
    sets a value later depending on performance considerations. To see the
    actual value, use `index_object:stat().range_size`.
]])

M['vinyl.read_threads'] = format_text([[
    The maximum number of read threads that vinyl can use for concurrent
    operations, such as I/O and compression.
]])

M['vinyl.run_count_per_level'] = format_text([[
    The maximum number of runs per level in the vinyl LSM tree. If this
    number is exceeded, a new level is created. The `vinyl.run_count_per_level`
    setting is a default value for the run_count_per_level option passed to
    `space_object:create_index()`.
]])

M['vinyl.run_size_ratio'] = format_text([[
    The ratio between the sizes of different levels in the LSM tree.
    The `vinyl.run_size_ratio` setting is a default value for the
    run_size_ratio option passed to `space_object:create_index()`.
]])

M['vinyl.timeout'] = format_text([[
    The vinyl storage engine has a scheduler that performs compaction.
    When vinyl is low on available memory, the compaction scheduler may
    be unable to keep up with incoming update requests. In that situation,
    queries may time out after vinyl.timeout seconds. This should rarely occur,
    since normally vinyl throttles inserts when it is running low on compaction
    bandwidth. Compaction can also be initiated manually with
    `index_object:compact()`.
]])

M['vinyl.write_threads'] = format_text([[
    The maximum number of write threads that vinyl can use for some
    concurrent operations, such as I/O and compression.
]])

-- }}} vinyl configuration

-- {{{ wal configuration

M['wal'] = format_text([[
    This section defines configuration parameters related to write-ahead log.
]])

M['wal.cleanup_delay'] = format_text([[
    The delay in seconds used to prevent the Tarantool garbage collector from
    immediately removing write-ahead log files after a node restart. This
    delay eliminates possible erroneous situations when the master deletes
    WALs needed by replicas after restart. As a consequence, replicas sync
    with the master faster after its restart and don't need to download all
    the data again. Once all the nodes in the replica set are up and
    running, a scheduled garbage collection is started again even if
    `wal.cleanup_delay` has not expired.

]])

M['wal.dir'] = format_text([[
    A directory where write-ahead log (`.xlog`) files are stored. A relative
    path in this option is interpreted as relative to `process.work_dir`.

    By default, WAL files and snapshots are stored in the same directory.
    However, you can set different values for the `wal.dir` and `snapshot.dir`
    options to store them on different physical disks for performance matters.
]])

M['wal.dir_rescan_delay'] = format_text([[
    The time interval in seconds between periodic scans of the write-ahead-log
    file directory, when checking for changes to write-ahead-log files for the
    sake of replication or hot standby.
]])

M['wal.ext'] = format_text([[
    This section describes options related to WAL extensions.
]])

M['wal.ext.new'] = format_text([[
    Enable storing a new tuple for each CRUD operation performed. The option
    is in effect for all spaces. To adjust the option for specific spaces,
    use the `wal.ext.spaces` option.
]])

M['wal.ext.old'] = format_text([[
    Enable storing an old tuple for each CRUD operation performed. The option
    is in effect for all spaces. To adjust the option for specific spaces,
    use the `wal.ext.spaces` option.
]])

M['wal.ext.spaces'] = format_text([[
    Enable or disable storing an old and new tuple in the WAL record for a
    given space explicitly. The configuration for specific spaces has
    priority over the configuration in the `wal.ext.new` and `wal.ext.old`
    options.

    The option is a key-value pair:

    - The key is a space name (string).
    - The value is a table that includes two optional boolean options:
    `old` and `new`. The format and the default value of these options are
    described in `wal.ext.old` and `wal.ext.new`.
]])

M['wal.ext.spaces.*'] = 'Space name.'

M['wal.ext.spaces.*.new'] = M['wal.ext.new']

M['wal.ext.spaces.*.old'] = M['wal.ext.old']

M['wal.max_size'] = format_text([[
    The maximum number of bytes in a single write-ahead log file. When a
    request would cause an `.xlog` file to become larger than `wal.max_size`,
    Tarantool creates a new WAL file.
]])

M['wal.mode'] = format_text([[
    Specify fiber-WAL-disk synchronization mode as: none: write-ahead log is
    not maintained. A node with wal.mode set to none can't be a replication
    master. write: fibers wait for their data to be written to the
    write-ahead log (no fsync(2)). fsync: fibers wait for their data,
    fsync(2) follows each write(2).

]])

M['wal.queue_max_size'] = format_text([[
    The size of the queue in bytes used by a replica to submit new transactions
    to a write-ahead log (WAL). This option helps limit the rate at which a
    replica submits transactions to the WAL. Limiting the queue size might be
    useful when a replica is trying to sync with a master and reads new
    transactions faster than writing them to the WAL.
]])

M['wal.retention_period'] = format_text([[
    The delay in seconds used to prevent the Tarantool garbage collector from
    removing a write-ahead log file after it has been closed. If a node is
    restarted, `wal.retention_period` counts down from the last modification
    time of the write-ahead log file.

    The garbage collector doesn't track write-ahead logs that are to be
    relayed to anonymous replicas, such as:

    - Anonymous replicas added as a part of a cluster configuration
    (see `replication.anon`).
    -CDC (Change Data Capture) that retrieves data using anonymous replication.

    In case of a replica or CDC downtime, the required write-ahead logs
    can be removed. As a result, such a replica needs to be rebootstrapped.
    You can use wal.retention_period to prevent such issues.

    Note that `wal.cleanup_delay` option also sets the delay used to prevent
    the Tarantool garbage collector from removing write-ahead logs. The
    difference is that the garbage collector doesn't take into account
    `wal.cleanup_delay` if all the nodes in the replica set are up and running,
    which may lead to the removal of the required write-ahead logs.
]])

-- }}} wal configuration

return M
