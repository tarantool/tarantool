local fun = require('fun')
local textutils = require('internal.config.utils.textutils')

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

-- {{{ Instance descriptions

local I = {}

I[''] = 'Instance configuration'

-- {{{ <uri>.params configuration

I['<uri>.params'] = format_text([[
    SSL parameters required for encrypted connections.
]])

I['<uri>.params.ssl_ca_file'] = format_text([[
    (Optional) A path to a trusted certificate authorities (CA) file. If not
    set, the peer won't be checked for authenticity.

    Both a server and a client can use the ssl_ca_file parameter:

    - If it's on the server side, the server verifies the client.
    - If it's on the client side, the client verifies the server.
    - If both sides have the CA files, the server and the client verify each
      other.
]])

I['<uri>.params.ssl_cert_file'] = format_text([[
    A path to an SSL certificate file:

    - For a server, it's mandatory.
    - For a client, it's mandatory if the ssl_ca_file parameter is set for a
      server; otherwise, optional.
]])

I['<uri>.params.ssl_ciphers'] = format_text([[
    (Optional) A colon-separated (:) list of SSL cipher suites the connection
    can use. Note that the list is not validated: if a cipher suite is unknown,
    Tarantool ignores it, doesn't establish the connection, and writes to the
    log that no shared cipher was found.
]])

I['<uri>.params.ssl_key_file'] = format_text([[
    A path to a private SSL key file:

    - For a server, it's mandatory.
    - For a client, it's mandatory if the `ssl_ca_file` parameter is set for a
      server; otherwise, optional.

    If the private key is encrypted, provide a password for it in the
    `ssl_password` or `ssl_password_file` parameter
]])

I['<uri>.params.ssl_password'] = format_text([[
    (Optional) A password for an encrypted private SSL key provided using
    `ssl_key_file`. Alternatively, the password can be provided in
    `ssl_password_file`.

    Tarantool applies the `ssl_password` and `ssl_password_file` parameters in
    the following order:

    - If `ssl_password` is provided, Tarantool tries to decrypt the
      private key with it.
    - If `ssl_password` is incorrect or isn't provided, Tarantool
      tries all passwords from `ssl_password_file` one by one in the
      order they are written.
    - If `ssl_password` and all passwords from `ssl_password_file`
      are incorrect, or none of them is provided, Tarantool treats
      the private key as unencrypted.
]])

I['<uri>.params.ssl_password_file'] = format_text([[
    (Optional) A text file with one or more passwords for encrypted private
    SSL keys provided using `ssl_key_file` (each on a separate line).
    Alternatively, the password can be provided in `ssl_password`.
]])

I['<uri>.params.transport'] = format_text([[
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

I['app'] = format_text([[
    Using Tarantool as an application server, you can run your own Lua
    applications. In the `app` section, you can load the application and
    provide an application configuration in the `app.cfg` section.
]])

I['app.cfg'] = format_text([[
    A configuration of the application loaded using `app.file` or `app.module`.
]])

I['app.cfg.*'] = format_text([[
    Mapping for arbitrary user-defined configuration values, accessible in
    the application via `config:get('app.cfg')`.
]])

I['app.file'] = 'A path to a Lua file to load an application from.'

I['app.module'] = 'A Lua module to load an application from.'

-- }}} app configuration

-- {{{ audit_log configuration

I['audit_log'] = format_text([[
    The `audit_log` section defines configuration parameters related to
    audit logging.
]])

I['audit_log.extract_key'] = format_text([[
    If set to `true`, the audit subsystem extracts and prints only the primary
    key instead of full tuples in DML events (`space_insert`, `space_replace`,
    `space_delete`). Otherwise, full tuples are logged. The option may be
    useful in case tuples are big.
]])

I['audit_log.file'] = format_text([[
    Specify a file for the audit log destination. You can set the `file` type
    using the audit_log.to option. If you write logs to a file, Tarantool
    reopens the audit log at SIGHUP.
]])

I['audit_log.filter'] = format_text([[
    Enable logging for a specified subset of audit events.
]])

I['audit_log.filter.*'] = format_text([[
    Specify a subset of audit events to log by providing a value
    from the allowed list of events or groups.
]])

I['audit_log.format'] = 'Specify a format that is used for the audit log.'

I['audit_log.nonblock'] = format_text([[
    Specify the logging behavior if the system is not ready to write.
    If set to `true`, Tarantool does not block during logging if the system
    is non-writable and writes a message instead. Using this value may
    improve logging performance at the cost of losing some log messages.
]])

I['audit_log.pipe'] = format_text([[
    Start a program and write logs to its standard input (`stdin`).
    To send logs to a program's standard input, you need to set
    `audit_log.to` to `pipe`.
]])

I['audit_log.spaces'] = format_text([[
    The array of space names for which data operation events (`space_select`,
    `space_insert`, `space_replace`, `space_delete`) should be logged. The array
    accepts string values. If set to box.NULL, the data operation events are
    logged for all spaces.
]])

I['audit_log.spaces.*'] = format_text([[
    A specific space name in the array for which data operation events are
    logged. Each entry must be a string representing the name of the space
    to monitor.

    Example:

    `spaces: [bands, singers]`, only the events of `bands` and `singers` spaces
    are logged.
]])

I['audit_log.to'] = 'Enable audit logging and define the log location.'

I['audit_log.syslog'] = format_text([[
    This module allows configuring the system logger (syslog) for audit
    logs in Tarantool. It provides options for specifying the syslog server,
    facility, and identity for logging messages.
]])

I['audit_log.syslog.facility'] = format_text([[
    Define the syslog facility, which indicates the type of application
    generating the log entries (e.g. kernel, user-level, or system daemon).
    To enable syslog logging, set `audit_log.to` to `syslog`.
]])

I['audit_log.syslog.identity'] = format_text([[
    Specify an application name to show in logs. You can enable logging
    to a system logger using the `audit_log.to` option.
]])

I['audit_log.syslog.server'] = format_text([[
    Set a location for the syslog server. It can be a Unix socket path
    starting with "unix:" or an ipv4 port number. You can enable logging
    to a system logger using the `audit_log.to` option.
]])

-- }}} audit_log configuration

-- {{{ compat configuration

I['compat'] = format_text([[
    These options allow to redefine tarantool behavior in order to correspond
    to the previous or the next major version.
]])

I['compat.binary_data_decoding'] = format_text([[
    Define how to store binary data fields in Lua after decoding:

    - `new` (3.x default): as varbinary object
    - `old` (2.x default): as plain strings
]])

I['compat.box_cfg_replication_sync_timeout'] = format_text([[
    Set a default replication sync timeout:

    - `new` (3.x default): 0
    - `old` (2.x default): 300 seconds
]])

I['compat.box_consider_system_spaces_synchronous'] = format_text([[
    Whether to consider most system spaces as synchronized regardless
    of the `is_sync` space option:

    - `new` (4.x default): System spaces are synchronized when
      the synchronous queue is claimed (`box.info.synchro.queue.owner ~= 0`),
      except for `vinyl_defer_delete` (local space) and `sequence_data`
      (synchronized by synchronous user spaces operations)
    - `old` (3.x default): System spaces are not synchronized unless
      explicitly marked with the `is_sync` option
]])

I['compat.box_error_serialize_verbose'] = format_text([[
    Set the verbosity of error objects serialization:

    - `new` (4.x default): serialize the error message together with other
      potentially useful fields
    - `old` (3.x default): serialize only the error message
]])

I['compat.box_error_unpack_type_and_code'] = format_text([[
    Whether to show all the error fields in `box.error.unpack()`:

    - `new` (4.x default): do not show `base_type` and `custom_type` fields; do
      not show the `code` field if it is 0. Note that `base_type` is still
      accessible for an error object
    - `old` (3.x default): show all fields
]])

I['compat.box_info_cluster_meaning'] = format_text([[
    Define the behavior of `box.info.cluster`:

    - `new` (3.x default): `box.info.cluster` shows info about the entire
      cluster, `box.info.replicaset` shows info about the replica set
    - `old` (2.x default): `box.info.cluster` shows info about the replica set
]])

I['compat.box_session_push_deprecation'] = format_text([[
    Whether to raise errors on attempts to call the deprecated function
    `box.session.push`:

    - `new` (4.x default): raise an error
    - `old` (3.x default): do not raise an error
]])

I['compat.box_space_execute_priv'] = format_text([[
    Whether the `execute` privilege can be granted on spaces:

    - `new` (3.x default): an error is raised
    - `old` (2.x default): the privilege can be granted with no actual effect
]])

I['compat.box_space_max'] = format_text([[
    Set the maximum space identifier (`box.schema.SPACE_MAX`):

    - `new` (3.x default): 2147483646
    - `old` (2.x default): 2147483647
]])

I['compat.box_tuple_extension'] = format_text([[
    Controls `IPROTO_FEATURE_CALL_RET_TUPLE_EXTENSION` and
    `IPROTO_FEATURE_CALL_ARG_TUPLE_EXTENSION` feature bits that define tuple
    encoding in iproto call and eval requests.

    - `new` (3.x default): tuples with formats are encoded as `MP_TUPLE`
    - `old` (2.x default): tuples with formats are encoded as `MP_ARRAY`
]])

I['compat.box_tuple_new_vararg'] = format_text([[
    Controls how `box.tuple.new` interprets an argument list:

    - `new` (3.x default): as a value with a tuple format
    - `old` (2.x default): as an array of tuple fields
]])

I['compat.c_func_iproto_multireturn'] = format_text([[
    Controls wrapping of multiple results of a stored C function when returning
    them via iproto:

    - `new` (3.x default): return without wrapping (consistently with a local
      call via `box.func`)
    - `old` (2.x default): wrap results into a MessagePack array
]])

I['compat.console_session_scope_vars'] = format_text([[
    Whether a console session has its own variable scope:

    - `new` (4.x default): non-local variable assignments
      are written to a variable scope attached to the console session
    - `old` (3.x default): all non-local variable assignments
      from the console are written to globals
]])

I['compat.fiber_channel_close_mode'] = format_text([[
    Define the behavior of fiber channels after closing:

    - `new` (3.x default): mark the channel read-only
    - `old` (2.x default): destroy the channel object
]])

I['compat.fiber_slice_default'] = format_text([[
    Define the maximum fiber execution time without a yield:

    - `new` (3.x default): `{warn = 0.5, err = 1.0}`
    - `old` (2.x default): infinity (no warnings or errors raised)
]])

I['compat.json_escape_forward_slash'] = format_text([[
    Whether to escape the forward slash symbol "/" using a backslash
    in a `json.encode()` result:

    - `new` (3.x default): do not escape the forward slash
    - `old` (2.x default): escape the forward slash
]])

I['compat.replication_synchro_timeout'] = format_text([[
    The `compat.replication_synchro_timeout` option controls transaction
    rollback due to `replication.synchro_timeout`.

    - `new` (4.x default): A synchronous transaction can remain in the synchro
      queue indefinitely until it reaches a quorum of confirmations.
      `replication.synchro_timeout` is used only to wait confirmation
      in promote/demote and gc-checkpointing. If some transaction in limbo
      did not have time to commit within `replication_synchro_timeout`,
      the corresponding operation: promote/demote or gc-checkpointing
      can be aborted automatically
    - `old` (3.x default): unconfirmed synchronous transactions are rolled back
      after a `replication.synchro_timeout`
]])

I['compat.sql_priv'] = format_text([[
    Whether to enable access checks for SQL requests over iproto:

    - `new` (3.x default): check the user's access permissions
    - `old` (2.x default): allow any user to execute SQL over iproto
]])

I['compat.sql_seq_scan_default'] = format_text([[
    Controls the default value of the `sql_seq_scan` session setting:

    - `new` (3.x default): false
    - `old` (2.x default): true
]])

I['compat.wal_cleanup_delay_deprecation'] = format_text([[
    Whether to use the option 'wal_cleanup_delay':

    - `new` (4.x default): raise an error
    - `old` (3.x default): log a deprecation warning
]])

I['compat.yaml_pretty_multiline'] = format_text([[
    Whether to encode in block scalar style all multiline strings or ones
    containing the `\n\n` substring:

    - `new` (3.x default): all multiline strings
    - `old` (2.x default): only strings containing the `\n\n` substring
]])

-- }}} compat configuration

-- {{{ config configuration

I['config'] = format_text([[
    The `config` section defines various parameters related to
    centralized configuration.
]])

-- {{{ config.context configuration

I['config.context'] = format_text([[
    Defines custom variables in the cluster configuration by loading
    values from an environment variable or a file.
]])

I['config.context.*'] = format_text([[
    A context variable definition that specifies how
    to load it (e.g. from a file or an environment variable).
]])

I['config.context.*.env'] = format_text([[
    The name of an environment variable to load a context variable from.
    To load a context variable from an environment variable, set
    `config.context.<name>.from` to `env`.
]])

I['config.context.*.file'] = format_text([[
    The path to a file to load a context variable from. To load a
    configuration value from a file, set `config.context.<name>.from` to `file`.
]])

I['config.context.*.from'] = format_text([[
    The type of storage to load a context variable from. There are the
    following storage types:

    - `file`: load a context variable from a file. In this case, you
      need to specify the path to the file using `config.context.<name>.file`
    - `env`: load a context variable from an environment variable.
      In this case, specify the environment variable name using
      `config.context.<name>.env`
]])

I['config.context.*.rstrip'] = format_text([[
    (Optional) Whether to strip whitespace characters and newlines
    from the end of data.
]])

-- }}} config.context configuration

-- {{{ config.etcd configuration

I['config.etcd'] = format_text([[
    This section describes options related to providing connection
    settings to a centralized etcd-based storage. If `replication.failover`
    is set to `supervised`, Tarantool also uses etcd to maintain the state
    of failover coordinators.
]])

I['config.etcd.endpoints'] = format_text([[
    The list of endpoints used to access an etcd cluster.
]])

I['config.etcd.endpoints.*'] = format_text([[
    etcd endpoint.

    For example: `http://localhost:2379`.
]])

I['config.etcd.http'] = format_text([[
    HTTP client options for the etcd-client, used to fetch and
    subscribe to the cluster configuration stored in etcd.
]])

I['config.etcd.http.request'] = 'HTTP client request options.'

I['config.etcd.http.request.interface'] = format_text([[
    Set the interface to use as outgoing network interface for the etcd
    configuration source.

    The interface can be specified as an interface name, an IP address, or a
    hostname.

    See https://curl.se/libcurl/c/CURLOPT_INTERFACE.html for details.
]])
I['config.etcd.http.request.timeout'] = format_text([[
    A time period required to process an HTTP request to an etcd server:
    from sending a request to receiving a response.
]])
I['config.etcd.http.request.unix_socket'] = format_text([[
    A Unix domain socket used to connect to an etcd server.
]])
I['config.etcd.http.request.verbose'] = format_text([[
    Whether to print debugging information about HTTP requests and responses
    issued by the etcd configuration source.

    The information is written to stderr (disregarding tarantool log
    configuration). In a typical setup it arrives to journald.

    See https://curl.se/libcurl/c/CURLOPT_VERBOSE.html for details.
]])

I['config.etcd.password'] = 'A password used for authentication.'

I['config.etcd.prefix'] = format_text([[
    A key prefix used to search a configuration on an etcd server.
    Tarantool searches keys by the following path: `<prefix>/config/*`.
    Note that `<prefix>` should start with a slash (`/`).
]])

I['config.etcd.ssl'] = 'TLS options.'

I['config.etcd.ssl.ca_file'] = format_text([[
    A path to a trusted certificate authorities (CA) file.
]])

I['config.etcd.ssl.ca_path'] = format_text([[
    A path to a directory holding certificates to verify the peer with.
]])

I['config.etcd.ssl.ssl_cert'] = 'A path to an SSL certificate file.'

I['config.etcd.ssl.ssl_key'] = 'A path to a private SSL key file.'

I['config.etcd.ssl.verify_host'] = format_text([[
    Enable verification of the certificate's name (CN) against
    the specified host.
]])

I['config.etcd.ssl.verify_peer'] = format_text([[
    Enable verification of the peer's SSL certificate.
]])

I['config.etcd.username'] = 'A username used for authentication.'

I['config.etcd.watchers'] = format_text([[
    Options for watcher requests: watchcreate, watchwait and watchcancel.
]])

I['config.etcd.watchers.reconnect_max_attempts'] = format_text([[
    The maximum number of attempts to reconnect to an etcd server in case
    of connection failure.
]])

I['config.etcd.watchers.reconnect_timeout'] = format_text([[
    The timeout (in seconds) between attempts to reconnect to an etcd
    server in case of connection failure.
]])

-- }}} config.etcd configuration

I['config.reload'] = format_text([[
    Specify how the configuration is reloaded. This option accepts the
    following values:

    - `auto`: configuration is reloaded automatically when it is changed.
    - `manual`: configuration should be reloaded manually. In this case, you can
      reload the configuration in the application code using `config:reload()`.
]])

-- {{{ config.storage configuration

I['config.storage'] = format_text([[
    This section describes options related to providing connection settings
    to a centralized Tarantool-based storage.
]])

I['config.storage.endpoints'] = format_text([[
    An array of endpoints used to access a configuration storage. Each endpoint
    can include the following fields:

    - `uri`: a URI of the configuration storage's instance.
    - `login`: a username used to connect to the instance.
    - `password`: a password used for authentication.
    - `params`: SSL parameters required for encrypted connections
]])

I['config.storage.endpoints.*'] = format_text([[
    Element that represents a configuration storage endpoint with the
    following fields:

    - `uri`: a URI of the configuration storage's instance.
    - `login`: a username used to connect to the instance.
    - `password`: a password used for authentication.
    - `params`: SSL parameters required for encrypted connections.
]])

I['config.storage.endpoints.*.login'] = format_text([[
    A username used to connect to the instance.
]])

I['config.storage.endpoints.*.params'] = I['<uri>.params']

I['config.storage.endpoints.*.params.ssl_ca_file'] =
    I['<uri>.params.ssl_ca_file']

I['config.storage.endpoints.*.params.ssl_cert_file'] =
    I['<uri>.params.ssl_cert_file']

I['config.storage.endpoints.*.params.ssl_ciphers'] =
    I['<uri>.params.ssl_ciphers']

I['config.storage.endpoints.*.params.ssl_key_file'] =
    I['<uri>.params.ssl_key_file']

I['config.storage.endpoints.*.params.ssl_password'] =
    I['<uri>.params.ssl_password']

I['config.storage.endpoints.*.params.ssl_password_file'] =
    I['<uri>.params.ssl_password_file']

I['config.storage.endpoints.*.params.transport'] =  I['<uri>.params.transport']

I['config.storage.endpoints.*.password'] = 'A password used for authentication.'

I['config.storage.endpoints.*.uri'] = format_text([[
    A URI of the configuration storage's instance.
]])

I['config.storage.prefix'] = format_text([[
    A key prefix used to search a configuration in a centralized configuration
    storage. Tarantool searches keys by the following path: `<prefix>/config/*`.
    Note that `<prefix>` should start with a slash (`/`).
]])

I['config.storage.reconnect_after'] = format_text([[
    A number of seconds to wait before reconnecting to a configuration storage.
]])

I['config.storage.timeout'] = format_text([[
    The interval (in seconds) to perform the status check of a configuration
    storage.
]])

-- }}} config.storage configuration

-- }}} config configuration

-- {{{ console configuration

I['console'] = format_text([[
    Configure the administrative console. A client to the
    console is `tt connect`.
]])

I['console.enabled'] = format_text([[
    Whether to listen on the Unix socket provided in the console.socket option.

    If the option is set to `false`, the administrative console is disabled.
]])

I['console.socket'] = format_text([[
    The Unix socket for the administrative console.

    Mind the following nuances:

    - Only a Unix domain socket is allowed. A TCP socket can't be configured
      this way.
    - `console.socket` is a file path, without any `unix:` or
      `unix/:` prefixes.
    - If the file path is a relative path, it is interpreted relative
      to `process.work_dir`.
]])

-- }}} console configuration

-- {{{ credentials configuration

I['credentials'] = format_text([[
    The `credentials` section allows you to create users and grant them the
    specified privileges.
]])

-- {{{ credentials.roles configuration

I['credentials.roles'] = format_text([[
    An array of roles that can be granted to users or other roles.
]])

I['credentials.roles.*'] = 'A role definition.'

I['credentials.roles.*.privileges'] = format_text([[
    An array of privileges granted to this role.
]])

I['credentials.roles.*.privileges.*'] = format_text([[
    Privileges that can be granted to a user with this role.
]])

I['credentials.roles.*.privileges.*.functions'] = format_text([[
    Registered functions to which user with this
    role gets the specified permissions.
]])

I['credentials.roles.*.privileges.*.functions.*'] = 'Function name.'

I['credentials.roles.*.privileges.*.lua_call'] = format_text([[
    Defines the Lua functions that the user with this role has permission to
    call. This field accepts a special value, `all`, which grants the privilege
    to use any global non-built-in Lua functions.
]])

I['credentials.roles.*.privileges.*.lua_call.*'] = 'Lua function name.'

I['credentials.roles.*.privileges.*.lua_eval'] = format_text([[
    Whether this user with this role can execute arbitrary Lua code.
]])

I['credentials.roles.*.privileges.*.permissions'] = format_text([[
    Permissions assigned to user with this role.
]])

I['credentials.roles.*.privileges.*.permissions.*'] = 'Permission name.'

I['credentials.roles.*.privileges.*.sequences'] = format_text([[
    Sequences to which user with this role gets the specified permissions.
]])

I['credentials.roles.*.privileges.*.sequences.*'] = 'Sequence name.'

I['credentials.roles.*.privileges.*.spaces'] = format_text([[
    Spaces to which user with this role gets the specified permissions.
]])

I['credentials.roles.*.privileges.*.spaces.*'] = 'Space name.'

I['credentials.roles.*.privileges.*.sql'] = format_text([[
    Whether user with this role can execute an arbitrary SQL expression.
]])
I['credentials.roles.*.privileges.*.sql.*'] = format_text([[
    SQL expression name.

    Only `all` is allowed for now.
]])

I['credentials.roles.*.privileges.*.universe'] = format_text([[
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

I['credentials.roles.*.roles'] = 'An array of roles granted to this role.'

I['credentials.roles.*.roles.*'] = 'Role name.'

-- }}} credentials.roles configuration

-- {{{ credentials.users configuration

I['credentials.users'] = 'An array of users.'

I['credentials.users.*'] = 'User name.'

I['credentials.users.*.password'] = format_text([[
    A user's password.
]])

I['credentials.users.*.privileges'] = format_text([[
    An array of privileges granted to this user.
]])

I['credentials.users.*.privileges.*'] = format_text([[
    Privileges that can be granted to a user.
]])

I['credentials.users.*.privileges.*.functions'] = format_text([[
    Registered functions to which this user gets the specified permissions.
]])
I['credentials.users.*.privileges.*.functions.*'] = 'Function name.'

I['credentials.users.*.privileges.*.lua_call'] = format_text([[
    Defines the Lua functions that the user has permission to call. This
    field accepts a special value, `all`, which grants the privilege
    to use any global non-built-in Lua functions.
]])
I['credentials.users.*.privileges.*.lua_call.*'] = 'Lua function name.'

I['credentials.users.*.privileges.*.lua_eval'] = format_text([[
    Whether this user can execute arbitrary Lua code.
]])

I['credentials.users.*.privileges.*.permissions'] = format_text([[
    Permissions assigned to this user or a user with this role.
]])

I['credentials.users.*.privileges.*.permissions.*'] = 'Permission name.'

I['credentials.users.*.privileges.*.sequences'] = format_text([[
    Sequences to which this user gets the specified permissions.
]])

I['credentials.users.*.privileges.*.sequences.*'] = 'Sequence name.'

I['credentials.users.*.privileges.*.spaces'] = format_text([[
    Spaces to which this user gets the specified permissions.
]])

I['credentials.users.*.privileges.*.spaces.*'] = 'Space name.'

I['credentials.users.*.privileges.*.sql'] = format_text([[
    Whether this user can execute an arbitrary SQL expression.
]])

I['credentials.users.*.privileges.*.sql.*'] = format_text([[
    SQL expression name.

    Only `all` is allowed for now.
]])

I['credentials.users.*.privileges.*.universe'] =
    I['credentials.roles.*.privileges.*.universe']

I['credentials.users.*.roles'] = 'An array of roles granted to this user.'

I['credentials.users.*.roles.*'] = 'Role name.'

-- }}} credentials.users configuration

-- }}} credentials configuration

-- {{{ database configuration

I['database'] = format_text([[
    The `database` section defines database-specific configuration parameters,
    such as an instance's read-write mode or transaction isolation level.
]])

I['database.hot_standby'] = format_text([[
    Whether to start the server in the hot standby mode. This mode can be used
    to provide failover without replication.

    Note: `database.hot_standby` has no effect:

    - If `wal.mode` is set to none.
    - If `wal.dir_rescan_delay` is set to a large value on macOS or FreeBSD.
      On these platforms, the hot standby mode is designed so that the loop
      repeats every `wal.dir_rescan_delay` seconds.
    - For spaces created with engine set to `vinyl`.
]])

I['database.instance_uuid'] = format_text([[
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

    Note: when upgrading from 2.x, `instance_uuid` and `replicaset_uuid`
    must be explicitly set in the configuration until the database schema
    upgrade is completed. After a full upgrade, these UUIDs can be removed
    from the configuration.
]])

I['database.mode'] = format_text([[
    An instance's operating mode. This option is in effect if
    `replication.failover` is set to `off`.

    The following modes are available:

    - `rw`: an instance is in read-write mode.
    - `ro`: an instance is in read-only mode.

    If not specified explicitly, the default value depends on the number of
    instances in a replica set. For a single instance, the `rw` mode is used,
    while for multiple instances, the `ro` mode is used.
]])

I['database.replicaset_uuid'] = format_text([[
    A replica set UUID.

    By default, replica set UUIDs are generated automatically.
    `database.replicaset_uuid` can be used to specify a replica set identifier
    manually.

    Note: when upgrading from 2.x, `instance_uuid` and `replicaset_uuid`
    must be explicitly set in the configuration until the database schema
    upgrade is completed. After a full upgrade, these UUIDs can be removed
    from the configuration.
]])

I['database.txn_isolation'] = 'A transaction isolation level.'

I['database.txn_timeout'] = format_text([[
    A timeout (in seconds) after which the transaction is rolled back.
]])

I['database.txn_synchro_timeout'] = format_text([[
    A timeout (in seconds) after which the fiber is detached from synchronous
    transaction that is currently collecting quorum. After the timeout expires,
    the transaction is not rolled back but continues to wait for a quorum in
    background.
]])

I['database.use_mvcc_engine'] = 'Whether the transactional manager is enabled.'

-- }}} database configuration

-- {{{ failover configuration

I['failover'] = format_text([[
    The `failover` section defines parameters related to a supervised failover.
]])

I['failover.call_timeout'] = format_text([[
    A call timeout (in seconds) for connections
    used by monitoring and autofailover components.
]])

I['failover.connect_timeout'] = format_text([[
    A connection timeout (in seconds) for connections
    used by monitoring and autofailover components.
]])

I['failover.lease_interval'] = format_text([[
    A time interval (in seconds) that specifies how long an instance should
    be a leader without renew requests from a coordinator. When this interval
    expires, the leader switches to read-only mode. This action is performed
    by the instance itself and works even if there is no connectivity between
    the instance and the coordinator.
]])

I['failover.log'] = format_text([[
    This section defines configuration parameters related
    to logging for the supervised failover coordinator.
]])

I['failover.log.file'] = format_text([[
    Specify a file for failover logs destination. To write logs
    to a file, you need to set `failover.log.to` to `file`.
    Otherwise, `failover.log.file` is ignored.
]])

I['failover.log.to'] = format_text([[
    Define the location for failover logs. This option accepts the
    following values:

    - `stderr`: write logs to the standard error stream
    - `file`: write logs to a file defined in `failover.log.file`
]])

I['failover.probe_interval'] = format_text([[
    A time interval (in seconds) that specifies how often a monitoring service
    of the failover coordinator polls an instance for its status.
]])

I['failover.renew_interval'] = format_text([[
    A time interval (in seconds) that specifies how often a failover coordinator
    sends read-write deadline renewals.
]])

I['failover.replicasets'] = format_text([[
    Failover coordinator options configured on the per-replicaset basis.
]])

I['failover.replicasets.*'] = format_text([[
    Failover coordinator options related to a particular replicaset.
]])

I['failover.replicasets.*.learners'] = format_text([[
    Specify instances that are ignored by the supervised failover
    coordinator when selecting a master.

    Note: if a learner instance is in RW mode, the coordinator stops the
    failover process and waits until the instance transitions to RO mode.
]])

I['failover.replicasets.*.learners.*'] = format_text([[
    Array of instance names to be ignored by the supervised failover
    coordinator when selecting a master.
]])

I['failover.replicasets.*.priority'] = format_text([[
    Priorities for the supervised failover mode.
]])

I['failover.replicasets.*.priority.*'] = format_text([[
    A failover priority assigned to the given instance.
]])

I['failover.stateboard'] = format_text([[
    This options define configuration parameters related to maintaining the
    state of failover coordinators in a remote etcd-based storage.
]])

I['failover.stateboard.enabled'] = format_text([[
    Enable or disable the failover coordinator stateboard.
]])

I['failover.stateboard.keepalive_interval'] = format_text([[
    A time interval (in seconds) that specifies how long a transient state
    information is stored and how quickly a lock expires.

    Note `failover.stateboard.keepalive_interval` should be smaller than
    `failover.lease_interval`. Otherwise, switching of a coordinator causes
    a replica set leader to go to read-only mode for some time.
]])

I['failover.stateboard.renew_interval'] = format_text([[
    A time interval (in seconds) that specifies how often a failover
    coordinator writes its state information to etcd. This option also
    determines the frequency at which an active coordinator reads new
    commands from etcd.
]])

-- }}} failover configuration

-- {{{ feedback configuration

I['feedback'] = format_text([[
    The `feedback` section describes configuration parameters for sending
    information about a running Tarantool instance to the specified feedback
    server.
]])

I['feedback.crashinfo'] = format_text([[
    Whether to send crash information in the case of an instance failure. This
    information includes:

    - General information from the `uname` output.
    - Build information.
    - The crash reason.
    - The stack trace.

    To turn off sending crash information, set this option to `false`.
]])

I['feedback.enabled'] = format_text([[
    Whether to send information about a running instance to the feedback
    server. To turn off sending feedback, set this option to `false`.
]])

I['feedback.host'] = 'The address to which information is sent.'

I['feedback.interval'] = 'The interval (in seconds) of sending information.'

I['feedback.metrics_collect_interval'] = format_text([[
    The interval (in seconds) for collecting metrics.
]])

I['feedback.metrics_limit'] = format_text([[
    The maximum size of memory (in bytes) used to store metrics before
    sending them to the feedback server. If the size of collected metrics
    exceeds this value, earlier metrics are dropped.
]])

I['feedback.send_metrics'] = format_text([[
    Whether to send metrics to the feedback server. Note that all collected
    metrics are dropped after sending them to the feedback server.
]])

-- }}} feedback configuration

-- {{{ fiber configuration

I['fiber'] = format_text([[
    The `fiber` section describes options related to configuring fibers, yields,
    and cooperative multitasking.
]])

I['fiber.io_collect_interval'] = format_text([[
    The time period (in seconds) a fiber sleeps between iterations of the
    event loop.

    `fiber.io_collect_interval` can be used to reduce CPU load in deployments
    where the number of client connections is large, but requests are not so
    frequent (for example, each connection issues just a handful of requests
    per second).
]])

I['fiber.slice'] = format_text([[
    This section describes options related to configuring time periods
    for fiber slices. See `fiber.set_max_slice` for details and examples.
]])

I['fiber.slice.err'] = format_text([[
    Set a time period (in seconds) that specifies the warning slice.
]])

I['fiber.slice.warn'] = format_text([[
    Set a time period (in seconds) that specifies the error slice.
]])

I['fiber.too_long_threshold'] = format_text([[
    If processing a request takes longer than the given period (in seconds),
    the fiber warns about it in the log.

    `fiber.too_long_threshold` has effect only if `log.level` is greater than
    or equal to 4 (`warn`).
]])

I['fiber.top'] = format_text([[
    This section describes options related to configuring the `fiber.top()`
    function, normally used for debug purposes. `fiber.top()` shows all alive
    fibers and their CPU consumption.
]])

I['fiber.top.enabled'] = format_text([[
    Enable or disable the `fiber.top()` function.

    Enabling `fiber.top()` slows down fiber switching by about 15%, so it is
    disabled by default.
]])

I['fiber.tx_user_pool_size'] = format_text([[
    Specify the size of the fiber pool used in the TX thread for executing
    user-defined callbacks pushed via the `tnt_tx_push()` C API function.
    This pool operates similarly to the fiber pool for handling IProto
    requests, whose size is defined by `box.cfg.net_msg_max`.

    Increase the pool size if the application requires executing a large
    number of concurrent callbacks, especially if they involve yielding
    operations or high transaction loads.

    Notes:

    - The callbacks are executed in the order they are pushed, but the
      completion order is undefined for yielding callbacks
    - Mismanaging the pool size or callback rate can lead to unpredictable
      latency or memory overflows (OOM)
]])

I['fiber.worker_pool_threads'] = format_text([[
    The maximum number of threads to use during execution of certain
    internal processes (for example, `socket.getaddrinfo()` and `coio_call()`).
]])

-- }}} fiber configuration

-- {{{ flightrec configuration

I['flightrec'] = format_text([[
    The flightrec section describes options related to the flight recorder
    configuration.
]])

I['flightrec.enabled'] = 'Enable the flight recorder.'

I['flightrec.logs_log_level'] = format_text([[
    Specify the level of detail the log has. The default value is 6
    (`VERBOSE`). You can learn more about log levels from the log_level
    option description. Note that the `flightrec.logs_log_level` value might
    differ from `log_level`.
]])

I['flightrec.logs_max_msg_size'] = format_text([[
    Specify the maximum size (in bytes) of the log message. The log message
    is truncated if its size exceeds this limit.
]])

I['flightrec.logs_size'] = format_text([[
    Specify the size (in bytes) of the log storage. You can set this option
    to 0 to disable the log storage.
]])

I['flightrec.metrics_interval'] = format_text([[
    Specify the time interval (in seconds) that defines the frequency
    of dumping metrics. This value shouldn't exceed `flightrec.metrics_period`.
]])

I['flightrec.metrics_period'] = format_text([[
    Specify the time period (in seconds) that defines how long metrics
    are stored from the moment of dump. So, this value defines how much
    historical metrics data is collected up to the moment of crash. The
    frequency of metric dumps is defined by `flightrec.metrics_interval`.
]])

I['flightrec.requests_max_req_size'] = format_text([[
    Specify the maximum size (in bytes) of a request entry.
    A request entry is truncated if this size is exceeded.
]])

I['flightrec.requests_max_res_size'] = format_text([[
    Specify the maximum size (in bytes) of a response entry.
    A response entry is truncated if this size is exceeded.
]])

I['flightrec.requests_size'] = format_text([[
    Specify the size (in bytes) of storage for the request and
    response data. You can set this parameter to 0 to disable
    a storage of requests and responses.
]])

-- }}} flightrec configuration

-- {{{ iproto configuration

I['iproto'] = format_text([[
    The iproto section is used to configure parameters related to communicating
    to and between cluster instances.
]])

-- {{{ iproto.advertise configuration

I['iproto.advertise'] = format_text([[
    URIs for cluster members and external clients
    to let them know where to connect.
]])

I['iproto.advertise.client'] = format_text([[
    A URI used to advertise the current instance to clients.

    The iproto.advertise.client option accepts a URI in the following formats:

    - An address: `host:port`.
    - A Unix domain socket: `unix/:`.

    Note that this option doesn't allow to set a username and password.
    If a remote client needs this information, it should be delivered outside
    of the cluster configuration.
]])

I['iproto.advertise.peer'] = format_text([[
    Settings used to advertise the current instance to other cluster members.
    The format of these settings is described in
    `iproto.advertise.<peer_or_sharding>.*`.
]])

I['iproto.advertise.peer.login'] = format_text([[
    (Optional) A username used to connect to the current instance.
    If a username is not set, the guest user is used.
]])

I['iproto.advertise.peer.params'] = I['<uri>.params']

I['iproto.advertise.peer.params.ssl_ca_file'] = I['<uri>.params.ssl_ca_file']

I['iproto.advertise.peer.params.ssl_cert_file'] =
    I['<uri>.params.ssl_cert_file']

I['iproto.advertise.peer.params.ssl_ciphers'] = I['<uri>.params.ssl_ciphers']

I['iproto.advertise.peer.params.ssl_key_file'] = I['<uri>.params.ssl_key_file']

I['iproto.advertise.peer.params.ssl_password'] = I['<uri>.params.ssl_password']

I['iproto.advertise.peer.params.ssl_password_file'] =
    I['<uri>.params.ssl_password_file']

I['iproto.advertise.peer.params.transport'] = I['<uri>.params.transport']

I['iproto.advertise.peer.password'] = format_text([[
    (Optional) A password for the specified user. If a login is specified but
    a password is missing, it is taken from the user's credentials.
]])

I['iproto.advertise.peer.uri'] = format_text([[
    (Optional) A URI used to advertise the current instance. By default,
    the URI defined in iproto.listen is used to advertise the current instance.
]])

I['iproto.advertise.sharding'] = format_text([[
    Settings used to advertise the current instance to a router and
    rebalancer. The format of these settings is described in
    `iproto.advertise.<peer_or_sharding>.*`.
]])

I['iproto.advertise.sharding.login'] = format_text([[
    (Optional) A username used to connect to the current instance.
    If a username is not set, the guest user is used.
]])

I['iproto.advertise.sharding.params'] = I['<uri>.params']

I['iproto.advertise.sharding.params.ssl_ca_file'] =
    I['<uri>.params.ssl_ca_file']

I['iproto.advertise.sharding.params.ssl_cert_file'] =
    I['<uri>.params.ssl_cert_file']

I['iproto.advertise.sharding.params.ssl_ciphers'] =
    I['<uri>.params.ssl_ciphers']

I['iproto.advertise.sharding.params.ssl_key_file'] =
    I['<uri>.params.ssl_key_file']

I['iproto.advertise.sharding.params.ssl_password'] =
    I['<uri>.params.ssl_password']

I['iproto.advertise.sharding.params.ssl_password_file'] =
    I['<uri>.params.ssl_password_file']

I['iproto.advertise.sharding.params.transport'] = I['<uri>.params.transport']

I['iproto.advertise.sharding.password'] = format_text([[
    (Optional) A password for the specified user. If a login is specified but
    a password is missing, it is taken from the user's credentials.
]])

I['iproto.advertise.sharding.uri'] = format_text([[
    (Optional) A URI used to advertise the current instance. By default,
    the URI defined in iproto.listen is used to advertise the current instance.
]])

-- }}} iproto.advertise configuration

I['iproto.listen'] = format_text([[
    An array of URIs used to listen for incoming requests. If required,
    you can enable SSL for specific URIs by providing additional parameters
    (`iproto.listen.*.params`).
]])

I['iproto.listen.*'] = format_text([[
    Iproto listening socket definition.

    Allows to set an URI (`unix/:<path>` or `host:port`) and SSL parameters.
    Minimal example: `{uri: 127.0.0.1:3301}`.
]])

I['iproto.listen.*.params'] =
    I['<uri>.params']

I['iproto.listen.*.params.ssl_ca_file'] =
    I['<uri>.params.ssl_ca_file']

I['iproto.listen.*.params.ssl_cert_file'] =
    I['<uri>.params.ssl_cert_file']

I['iproto.listen.*.params.ssl_ciphers'] =
    I['<uri>.params.ssl_ciphers']

I['iproto.listen.*.params.ssl_key_file'] =
    I['<uri>.params.ssl_key_file']

I['iproto.listen.*.params.ssl_password'] =
    I['<uri>.params.ssl_password']

I['iproto.listen.*.params.ssl_password_file'] =
    I['<uri>.params.ssl_password_file']

I['iproto.listen.*.params.transport'] = I['<uri>.params.transport']

I['iproto.listen.*.uri'] = format_text([[
    An array of URIs used to listen for incoming requests. If required,
    you can enable SSL for specific URIs by providing additional parameters
    (`iproto.listen.*.params`).

    Note: the `iproto.listen.*.uri` string can't contain a login or a password,
    it has no sense for a listening socket.

    The query-parameter form of setting SSL options is forbidden
    in the URI string. Use the `iproto.listen.*.params` for them.
]])

I['iproto.net_msg_max'] = format_text([[
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

I['iproto.readahead'] = format_text([[
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

I['iproto.threads'] = format_text([[
    The number of network threads. There can be unusual workloads where the
    network thread is 100% loaded and the transaction processor thread is not,
    so the network thread is a bottleneck. In that case, set `iproto_threads`
    to 2 or more. The operating system kernel determines which connection goes
    to which thread.
]])

-- }}} iproto configuration

-- {{{ isolated configuration

I['isolated'] = format_text([[
    Temporarily isolate an instance to perform replicaset repairing activities,
    such as debugging a problem on the isolated instance without affecting
    the non-isolated part or extracting data from the isolated instance
    to apply on the non-isolated part of the replicaset.

    Effects of isolation:

    - The instance stops listening for new IProto connections.
    - All current IProto connections are dropped.
    - The instance switches to read-only mode.
    - The instance disconnects from all replication upstreams.
    - Other replicaset members exclude the isolated instance from their
      replication upstreams.

    Note: the isolated instance can't be bootstrapped (a local snapshot is
    required to start).
]])

-- }}} isolated configuration

-- {{{ labels configuration

I['labels'] = format_text([[
    The `labels` section allows adding custom attributes
    to the instance. The keys and values are strings.
]])

I['labels.*'] = 'A value of the label with the specified name.'

-- }}} labels configuration

-- {{{ log configuration

I['log'] = format_text([[
    The `log` section defines configuration parameters related to logging.
    To handle logging in your application, use the log module.
]])

I['log.file'] = format_text([[
    Specify a file for logs destination. To write logs to a file, you need
    to set `log.to` to file. Otherwise, `log.file` is ignored.
]])

I['log.format'] = format_text([[
    Specify a format that is used for a log entry. The following formats
    are supported:

    - `plain`: a log entry is formatted as plain text.
    - `json`: a log entry is formatted as JSON and includes additional fields.
]])

I['log.level'] = format_text([[
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

I['log.modules'] = format_text([[
    Configure the specified log levels (`log.level`) for different modules.

    You can specify a logging level for the following module types:

    - Modules (files) that use the default logger.
    - Modules that use custom loggers created using the `log.new()` function.
    - The tarantool module that enables you to configure the logging level
      for Tarantool core messages. Specifically, it configures the logging level
      for messages logged from non-Lua code, including C modules.
]])

I['log.modules.*'] = format_text([[
    The log level.

    For example: you have module placed by the following path:
    `test/module.lua`. To configure logging levels, you need to
    provide module names corresponding to paths to these modules:
    `test.module: 'verbose'`.
]])

I['log.nonblock'] = format_text([[
    Specify the logging behavior if the system is not ready to write.
    If set to `true`, Tarantool does not block during logging if the system
    is non-writable and writes a message instead. Using this value may
    improve logging performance at the cost of losing some log messages.
]])

I['log.pipe'] = format_text([[
    Start a program and write logs to its standard input (`stdin`). To send
    logs to a program's standard input, you need to set `log.to` to `pipe`.
]])

I['log.syslog'] = format_text([[
    Syslog configurations parameters. To write logs to syslog,
    you need to set `log.to` to `syslog`.
]])

I['log.syslog.facility'] = format_text([[
    Specify the syslog facility to be used when syslog is enabled.
    To write logs to syslog, you need to set `log.to` to `syslog`.
]])

I['log.syslog.identity'] = format_text([[
    Specify an application name used to identify Tarantool messages in
    syslog logs. To write logs to syslog, you need to set `log.to` to `syslog`.
]])

I['log.syslog.server'] = format_text([[
    Set a location of a syslog server. This option accepts one of the following
    values:

    - An address. Example: `127.0.0.1:514`.
    - A Unix socket path starting with `unix:`.
      Examples: `unix:/dev/log` on Linux or `unix:/var/run/syslog` on macOS.

    To write logs to syslog, you need to set `log.to` to `syslog`.
]])

I['log.to'] = format_text([[
    Define a location Tarantool sends logs to. This option accepts the
    following values:

    - `stderr`: write logs to the standard error stream.
    - `file`: write logs to a file.
    - `pipe`: start a program and write logs to its standard input.
    - `syslog`: write logs to a system logger.
]])

-- }}} log configuration

-- {{{ lua configuration

I['lua'] = format_text([[
    This section defines configuration parameters related
    to Lua within Tarantool.
]])

I['lua.memory'] = format_text([[
    Define amount of memory available to Lua in bytes.
    Default is 2GB, with a minimum of 256MB.

    The limit can be adjusted dynamically if the new value is greater
    than the used memory amount. Otherwise, a restart is required for
    changes to take effect.
]])

-- }}} lua configuration

-- {{{ memtx configuration

I['memtx'] = format_text([[
   This section is used to configure parameters related to the memtx engine.
]])

I['memtx.allocator'] = format_text([[
    Specify the allocator that manages memory for memtx tuples. Possible values:

    - `system` - the memory is allocated as needed, checking that the quota
      is not exceeded. The allocator is based on the `malloc` function.
    - `small` - a slab allocator. The allocator repeatedly uses a memory
      block to allocate objects of the same type. Note that this allocator is
      prone to unresolvable fragmentation on specific workloads, so you can
      switch to `system` in such cases.
]])

I['memtx.max_tuple_size'] = format_text([[
    Size of the largest allocation unit for the memtx storage engine in bytes.
    It can be increased if it is necessary to store large tuples.
]])

I['memtx.memory'] = format_text([[
    The amount of memory in bytes that Tarantool allocates to store tuples.
    When the limit is reached, `INSERT` and `UPDATE` requests fail with the
    `ER_MEMORY_ISSUE` error. The server does not go beyond the `memtx.memory`
    limit to allocate tuples, but there is additional memory used to store
    indexes and connection information.
]])

I['memtx.min_tuple_size'] = format_text([[
    Size of the smallest allocation unit in bytes. It can be decreased if
    most of the tuples are very small.
]])

I['memtx.slab_alloc_factor'] = format_text([[
    The multiplier for computing the sizes of memory chunks that tuples
    are stored in. A lower value may result in less wasted memory depending
    on the total amount of memory available and the distribution of item sizes.
]])

I['memtx.slab_alloc_granularity'] = format_text([[
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

I['memtx.sort_threads'] = format_text([[
    The number of threads from the thread pool used to sort keys of secondary
    indexes on loading a `memtx` database. The minimum value is 1, the maximum
    value is 256. The default is to use all available cores.
]])

-- }}} memtx configuration

-- {{{ metrics configuration

I['metrics'] = format_text([[
    The `metrics` section provides the ability to collect and expose
    Tarantool metrics (e.g. network, cpu, memtx and others).
]])

I['metrics.exclude'] = format_text([[
    An array containing groups of metrics to turn off. The array can
    contain the same values as the `exclude` configuration parameter
    passed to `metrics.cfg()`.
]])

I['metrics.exclude.*'] = 'A name of a group of metrics.'

I['metrics.include'] = format_text([[
    An array containing groups of metrics to turn on. The array can
    contain the same values as the `include` configuration parameter
    passed to `metrics.cfg()`.
]])

I['metrics.include.*'] = 'A name of a group of metrics.'

I['metrics.labels'] = 'Global labels to be added to every observation.'

I['metrics.labels.*'] = 'Label value.'

-- }}} metrics configuration

-- {{{ process configuration

I['process'] = format_text([[
    The `process` section defines configuration parameters of the Tarantool
    process in the system.
]])

I['process.background'] = format_text([[
    Run the server as a daemon process.

    If this option is set to true, Tarantool log location defined by the
    `log.to` option should be set to file, pipe, or syslog - anything other
    than stderr, the default, because a daemon process is detached from a
    terminal and it can't write to the terminal's stderr.

    Warn: Do not enable the background mode for applications intended
    to run by the tt utility.
]])

I['process.coredump'] = format_text([[
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

I['process.pid_file'] = format_text([[
    Store the process id in this file.

    This option may contain a relative file path. In this case, it is
    interpreted as relative to `process.work_dir`.
]])

I['process.strip_core'] = format_text([[
    Whether coredump files should not include memory allocated for tuples -
    this memory can be large if Tarantool runs under heavy load. Setting to
    `true` means "do not include".
]])

I['process.title'] = format_text([[
    Add the given string to the server's process title (it is shown in the
    COMMAND column for the Linux commands `ps -ef` and `top -c`).
]])

I['process.username'] = 'The name of the system user to switch to after start.'

I['process.work_dir'] = format_text([[
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

I['replication'] = format_text([[
    This section defines configuration parameters related to replication.
]])

I['replication.anon'] = format_text([[
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

I['replication.anon_ttl'] = format_text([[
    Time-to-live (in seconds) of disconnected anonymous replicas (see
    `replication.anon` for the definition of anonymous replica). If an anonymous
    replica hasn't been in touch for longer than `replication.anon_ttl`, it is
    removed from the instance.
]])

I['replication.autoexpel'] = format_text([[
    Automatically expel instances.

    The option is useful for management of dynamic clusters using the YAML
    configuration. The option allows to automatically expel instances
    that are removed from the YAML configuration.

    Only instances whose names start from the given prefix are taken into
    account, all the others are ignored. Also, instances without a
    persistent name set are ignored too.

    If an instance is in read-write mode and has a latest database schema,
    it performs expelling of the instances:

    - with the given prefix, *and*
    - not present in the YAML configuration.

    The expelling process the usual one: deletion from the `_cluster` system
    space.

    The autoexpel logic works on startup and reacts on the reconfiguration
    and the `box.status` watcher event. If a new instance is joined and
    neither of these two events occur, autoexpel does not perform any
    actions on it. In other words, it doesn't forbid joining of an instance
    that met the autoexpel criterion.

    The option is allowed on the `replicaset`, `group` and `global` levels,
    but forbidden on the `instance` level of the cluster configuration.
]])

I['replication.autoexpel.by'] = format_text([[
    The autoexpel criterion: it defines how to determine that an instance is
    part of the cluster configuration and is not an external service that
    uses the replication channel (such as a CDC tool).

    Now, only `replication.autoexpel.by` = `prefix` criterion is supported.
    A user have to set it explicitly.

    In future we can provide other criteria and set one of them as default.
]])

I['replication.autoexpel.enabled'] = format_text([[
    Determines, whether the autoexpelling logic is enabled at all. If the option
    is set, `replication.autoexpel.by` and `replication.autoexpel.prefix` are
    required.
]])

I['replication.autoexpel.prefix'] = format_text([[
    Defines a pattern for instance names that are considered a part of the
    cluster (not some external services).

    For example, if all the instances in the cluster configuration are prefixed
    with the replica set name, one can use `replication.autoexpel.prefix` =
    '{{ replicaset_name }}'`.

    If all the instances follow the `i-\d\d\d` pattern, the option can be set
    to `i-`.
]])

I['replication.bootstrap_strategy'] = format_text([[
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
      The bootstrap leader management is in the user's responsibility unless the
      failover coordinator is in use (replication.failover = supervised).
    - `native`: the bootstrap leader management is performed by config's code
      in sync with the RO/RW management (the algorithm depends on
      replication.failover). If replication.failover = supervised, then the
      failover coordinator manages the bootstrap leader.

      This strategy is similar to `auto` from the user perspective: everything
      is handled by tarantool (or coordinator) on its own. However, it is based
      on the modern `supervised` strategy, which allows to overcome some
      limitations.
    - `legacy` (deprecated since 2.11.0): a node requires the
      `replication_connect_quorum` number of other nodes to be connected. This
      option is added to keep the compatibility with the current versions of
      Cartridge and might be removed in the future.

    Note: when using bootstrap strategies `supervised` or `native` with a
    supervised failover (see `replication.failover` configuration option)
    Tarantool automatically grants the guest user privileges allowing to execute
    the internal `failover.execute` call for performing the initial cluster
    bootstrap.
]])

I['replication.connect_timeout'] = format_text([[
    A timeout (in seconds) a replica waits when trying to connect to a master
    in a cluster.

    This parameter is different from replication.timeout, which a master uses
    to disconnect a replica when the master receives no acknowledgments of
    heartbeat messages.
]])

I['replication.election_fencing_mode'] = format_text([[
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

I['replication.election_mode'] = format_text([[
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

I['replication.election_timeout'] = format_text([[
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

I['replication.failover'] = format_text([[
    A failover mode used to take over a master role when the current master
    instance fails. The following modes are available:

    - `off`: Leadership in a replica set is controlled using the
      `database.mode` option. In this case, you can set the `database.mode`
      option to rw on all instances in a replica set to make a master-master
      configuration.
    - `manual`: Leadership in a replica set is controlled using the
      `<replicaset_name>.leader` option. In this case, a master-master
      configuration is forbidden.
    - `election`: Automated leader election is used to control leadership in a
      replica set.
    - `supervised`: (Enterprise Edition only) Leadership in a replica set is
      controlled using an external failover coordinator.

    Notes:

    In the `off` mode, the default `database.mode` is determined as follows:
    `rw` if there is onecinstance in a replica set; `ro` if there are several
    instances.

    In the `manual` mode, the `database.mode` option cannot be set explicitly.
    The leader is configured in the read-write mode, all the other instances
    are read-only.

    In the `election` mode and the `supervised` mode, `database.mode` and
    `<replicaset_name>.leader` shouldn't be set explicitly.
]])

I['replication.peers'] = format_text([[
    URIs of instances that constitute a replica set. These URIs are used by
    an instance to connect to another instance as a replica.

    Alternatively, you can use iproto.advertise.peer to specify a URI used to
    advertise the current instance to other cluster members.
]])

I['replication.peers.*'] = format_text([[
    Specifies the URI of the instance.

    For example: `replicator:topsecret@127.0.0.1:3301`.
]])

I['replication.skip_conflict'] = format_text([[
    By default, if a replica adds a unique key that another replica has added,
    replication stops with the `ER_TUPLE_FOUND` error. If
    `replication.skip_conflict` is set to `true`, such errors are ignored.
]])

I['replication.sync_lag'] = format_text([[
    The maximum delay (in seconds) between the time when data is written to
    the master and the time when it is written to a replica.

    If a replica should remain in the synched status disregarding of the
    network delay, set this option to a large value.
]])

I['replication.sync_timeout'] = format_text([[
    The timeout (in seconds) that a node waits when trying to sync with other
    nodes in a replica set after connecting or during a configuration update.
    This could fail indefinitely if `replication.sync_lag` is smaller than
    network latency, or if the replica cannot keep pace with master updates.
    If `replication.sync_timeout` expires, the replica enters `orphan` status.
]])

I['replication.synchro_queue_max_size'] = format_text([[
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

I['replication.synchro_quorum'] = format_text([[
    A number of replicas that should confirm the receipt of a synchronous
    transaction before it can finish its commit.

    This option supports dynamic evaluation of the quorum number. For example,
    the default value is `N / 2 + 1` where `N` is the current number of replicas
    registered in a replica set. Once any replicas are added or removed, the
    expression is re-evaluated automatically.

    Note that the default value (`at least 50% of the replica set size + 1`)
    guarantees data reliability. Using a value less than the canonical one
    might lead to unexpected results, including a split-brain.

    `replication.synchro_quorum` is not used on replicas. If the master fails,
    the pending synchronous transactions will be kept waiting on the replicas
    until a new master is elected.
]])

I['replication.synchro_timeout'] = format_text([[
    For synchronous replication only. Specify how many seconds to wait for a
    synchronous transaction quorum replication until it is declared failed and
    is rolled back.

    It is not used on replicas, so if the master fails, the pending synchronous
    transactions will be kept waiting on the replicas until a new master is
    elected.
]])

I['replication.threads'] = format_text([[
    The number of threads spawned to decode the incoming replication data.

    In most cases, one thread is enough for all incoming data. Possible values
    range from 1 to 1000. If there are multiple replication threads, connections
    to serve are distributed evenly between the threads.
]])

I['replication.timeout'] = format_text([[
    A time interval (in seconds) used by a master to send heartbeat requests to
    a replica when there are no updates to send to this replica. For each
    request, a replica should return a heartbeat acknowledgment.

    If a master or replica gets no heartbeat message for
    `4 * replication.timeout` seconds, a connection is dropped and a replica
    tries to reconnect to the master.
]])


I['replication.reconnect_timeout'] = format_text([[
    The timeout (in seconds) between attempts to reconnect to a master
    in case of connection failure. Default is box.NULL. If the option is
    set to box.NULL, then it equals to replication_timeout.
]])

-- }}} replication configuration

-- {{{ roles configuration

I['roles'] = format_text([[
    Specify the roles of an instance. To specify a role's configuration,
    use the roles_cfg option.
]])

I['roles.*'] = format_text([[
    The name of a role, corresponding to the module name used in the
    `require` call to load the role.
]])

-- }}} roles configuration

-- {{{ roles_cfg configuration

I['roles_cfg'] = format_text([[
    Specify a role's configuration. This option accepts a role name as the
    key and a role's configuration as the value. To specify the roles of an
    instance, use the roles option.
]])

I['roles_cfg.*'] = 'Configuration of the given role.'

-- }}} roles_cfg configuration

-- {{{ security configuration

I['security'] = format_text([[
    This section defines configuration parameters related to various
    security settings.
]])

I['security.auth_delay'] = format_text([[
    Specify a period of time (in seconds) that a specific user should wait for
    the next attempt after failed authentication.

    The `security.auth_retries` option lets a client try to authenticate the
    specified number of times before `security.auth_delay` is enforced.
]])

I['security.auth_retries'] = format_text([[
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

I['security.auth_type'] = format_text([[
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

I['security.disable_guest'] = format_text([[
    If `true`, turn off access over remote connections from unauthenticated or
    guest users. This option affects connections between cluster members and
    `net.box` connections.
]])

I['security.password_enforce_digits'] = format_text([[
    If true, a password should contain digits (0-9).
]])

I['security.password_enforce_lowercase'] = format_text([[
    If true, a password should contain lowercase letters (a-z).
]])

I['security.password_enforce_specialchars'] = format_text([[
    If true, a password should contain at least one special character
    (such as &|?!@$).
]])

I['security.password_enforce_uppercase'] = format_text([[
    If true, a password should contain uppercase letters (A-Z).
]])

I['security.password_history_length'] = format_text([[
    Specify the number of unique new user passwords before an old password
    can be reused. Note tarantool uses the auth_history field in the
    `box.space._user` system space to store user passwords.
]])

I['security.password_lifetime_days'] = format_text([[
    Specify the maximum period of time (in days) a user can use the
    same password. When this period ends, a user gets the "Password expired"
    error on a login attempt. To restore access for such users,
    use `box.schema.user.passwd`.
]])

I['security.password_min_length'] = format_text([[
    Specify the minimum number of characters for a password.
]])

I['security.secure_erasing'] = format_text([[
    If `true`, forces Tarantool to overwrite a data file a few times before
    deletion to render recovery of a deleted file impossible. The option
    applies to both `.xlog` and `.snap` files as well as Vinyl data files.
]])

-- }}} security configuration

-- {{{ sharding configuration

I['sharding'] = format_text([[
    This section defines configuration parameters related to sharding.
]])

I['sharding.bucket_count'] = format_text([[
    The total number of buckets in a cluster.
]])

I['sharding.connection_outdate_delay'] = format_text([[
    Time to outdate old objects on reload.
]])

I['sharding.discovery_mode'] = format_text([[
    A mode of the background discovery fiber used by the router to find buckets.
]])

I['sharding.failover_ping_timeout'] = format_text([[
    The timeout (in seconds) after which a node is considered unavailable if
    there are no responses during this period. The failover fiber is used to
    detect if a node is down.
]])

I['sharding.lock'] = format_text([[
    Whether a replica set is locked. A locked replica set cannot receive new
    buckets nor migrate its own buckets.
]])

I['sharding.rebalancer_disbalance_threshold'] = format_text([[
    The maximum bucket disbalance threshold (in percent). The disbalance
    is calculated for each replica set using the following formula:

    `|etalon_bucket_count - real_bucket_count| / etalon_bucket_count * 100`
]])

I['sharding.rebalancer_max_receiving'] = format_text([[
    The maximum number of buckets that can be received in parallel by a single
    replica set. This number must be limited because the rebalancer sends a
    large number of buckets from the existing replica sets to the newly added
    one. This produces a heavy load on the new replica set.
]])

I['sharding.rebalancer_max_sending'] = format_text([[
    The degree of parallelism for parallel rebalancing.
]])

I['sharding.rebalancer_mode'] = format_text([[
    Configure how a rebalancer is selected:

    - `auto` (default): if there are no replica sets with the rebalancer
      sharding role (`sharding.roles`), a replica set with the rebalancer is
      selected automatically among all replica sets.
    - `manual`: one of the replica sets should have the rebalancer sharding
      role. The rebalancer is in this replica set.
    - `off`: rebalancing is turned off regardless of whether a replica set
      with the rebalancer sharding role exists or not.
]])

I['sharding.roles'] = format_text([[
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

I['sharding.roles.*'] = 'Sharding role: router, storage or rebalancer.'

I['sharding.sched_move_quota'] = format_text([[
    A scheduler's bucket move quota used by the rebalancer.

    `sched_move_quota` defines how many bucket moves can be done in a row if
    there are pending storage refs. Then, bucket moves are blocked and a router
    continues making map-reduce requests.
]])

I['sharding.sched_ref_quota'] = format_text([[
    A scheduler's storage ref quota used by a router's map-reduce API.
    For example, the `vshard.router.map_callrw()` function implements
    consistent map-reduce over the entire cluster.

    `sched_ref_quota` defines how many storage refs, therefore map-reduce
    requests, can be executed on the storage in a row if there are pending
    bucket moves. Then, storage refs are blocked and the rebalancer continues
    bucket moves.
]])

I['sharding.shard_index'] = format_text([[
    The name or ID of a TREE index over the bucket id. Spaces without this
    index do not participate in a sharded Tarantool cluster and can be used
    as regular spaces if needed. It is necessary to specify the first part of
    the index, other parts are optional.
]])

I['sharding.sync_timeout'] = format_text([[
    The timeout to wait for synchronization of the old master with replicas
    before demotion. Used when switching a master or when manually calling
    the `sync()` function.
]])

I['sharding.weight'] = format_text([[
    The relative amount of data that a replica set can store.
]])

I['sharding.zone'] = format_text([[
    A zone that can be set for routers and replicas. This allows sending
    read-only requests not only to a master instance but to any available
    replica that is the nearest to the router.
]])

-- }}} sharding configuration

-- {{{ snapshot configuration

I['snapshot'] = format_text([[
    This section defines configuration parameters related to the
    snapshot files.
]])

I['snapshot.by'] = format_text([[
    An object containing configuration options that specify the conditions
    under which automatic snapshots are created by the checkpoint daemon.
    This includes settings like `interval` for time-based snapshots and
    `wal_size` for snapshots triggered when the total size of WAL files
    exceeds a certain threshold.
]])

I['snapshot.by.interval'] = format_text([[
    The interval in seconds between actions by the checkpoint daemon. If
    the option is set to a value greater than zero, and there is activity
    that causes change to a database, then the checkpoint daemon calls
    `box.snapshot()` every `snapshot.by.interval` seconds, creating a new
    snapshot file each time. If the option is set to zero, the checkpoint
    daemon is disabled.
]])

I['snapshot.by.wal_size'] = format_text([[
    The threshold for the total size in bytes for all WAL files created
    since the last snapshot taken. Once the configured threshold is exceeded,
    the WAL thread notifies the checkpoint daemon that it must make a new
    snapshot and delete old WAL files.
]])

I['snapshot.count'] = format_text([[
    The maximum number of snapshots that are stored in the `snapshot.dir`
    directory. If the number of snapshots after creating a new one exceeds
    this value, the Tarantool garbage collector deletes old snapshots.
    If `snapshot.count` is set to zero, the garbage collector does not delete
    old snapshots.
]])

I['snapshot.dir'] = format_text([[
    A directory where memtx stores snapshot (`.snap`) files. A relative path
    in this option is interpreted as relative to `process.work_dir`.

    By default, snapshots and WAL files are stored in the same directory.
    However, you can set different values for the `snapshot.dir` and `wal.dir`
    options to store them on different physical disks for performance matters.
]])

I['snapshot.snap_io_rate_limit'] = format_text([[
    Reduce the throttling effect of `box.snapshot()` on `INSERT/UPDATE/DELETE`
    performance by setting a limit on how many megabytes per second it can
    write to disk. The same can be achieved by splitting `wal.dir` and
    `snapshot.dir` locations and moving snapshots to a separate disk. The
    limit also affects what `box.stat.vinyl().regulator` may show for the write
    rate of dumps to `.run` and `.index` files.
]])

-- }}} snapshot configuration

-- {{{ sql configuration

I['sql'] = 'This section defines configuration parameters related to SQL.'

I['sql.cache_size'] = format_text([[
    The maximum cache size (in bytes) for all SQL prepared statements.
    To see the actual cache size, use `box.info.sql().cache.size`.
]])

-- }}} sql configuration

-- {{{ stateboard configuration

I['stateboard'] = format_text([[
    These options define configuration parameters related to the stateboard
    service allowing Tarantool instances to report their state into some extra
    key-value storage (e.g. etcd or Tarantool config.storage).

    An instance with an enabled stateboard reports its status to
    `<prefix>/state/by-name/{{ instance_name }}` where prefix is received from
    the `config.*.prefix` option. The provided information is in YAML format
    with the following fields:

    - `hostname` (`string`): hostname.
    - `pid` (`integer`): Tarantool process ID.
    - `mode` (`'ro'` or `'rw'`): instance mode (see `box.info.ro`).
    - `ro_reason` (`string`): the reason why the instance is read-only (see
      `box.info.ro_reason`).
    - `status` (`string`): instance status (see `box.info.status` for possible
      values and their description).
]])

I['stateboard.enabled'] = format_text([[
    Enable or disable the stateboard service.
]])

I['stateboard.keepalive_interval'] = format_text([[
    A time interval (in seconds) that specifies how long a transient state
    information is stored.
]])

I['stateboard.renew_interval'] = format_text([[
    A time interval (in seconds) that specifies how often a Tarantool instance
    writes its state information to the stateboard.
]])

-- }}} stateboard configuration

-- {{{ vinyl configuration

I['vinyl'] = format_text([[
   This section defines configuration parameters related to
   the vinyl storage engine.
]])

I['vinyl.bloom_fpr'] = format_text([[
    A bloom filter's false positive rate - the suitable probability of the
    bloom filter to give a wrong result. The `vinyl.bloom_fpr` setting is a
    default value for the bloom_fpr option passed to
    `space_object:create_index()`.
]])

I['vinyl.cache'] = format_text([[
    The cache size for the vinyl storage engine. The cache can
    be resized dynamically.
]])

I['vinyl.defer_deletes'] = format_text([[
    Enable the deferred DELETE optimization in vinyl. It was disabled by
    default since Tarantool version 2.10 to avoid possible performance
    degradation of secondary index reads.
]])

I['vinyl.dir'] = format_text([[
    A directory where vinyl files or subdirectories will be stored. This option
    may contain a relative file path. In this case, it is interpreted as
    relative to `process.work_dir`.
]])

I['vinyl.max_tuple_size'] = format_text([[
    The size of the largest allocation unit, for the vinyl storage engine.
    It can be increased if it is necessary to store large tuples.
]])

I['vinyl.memory'] = format_text([[
    The maximum number of in-memory bytes that vinyl uses.
]])

I['vinyl.page_size'] = format_text([[
    The page size. A page is a read/write unit for vinyl disk operations.
    The `vinyl.page_size` setting is a default value for the page_size option
    passed to `space_object:create_index()`.
]])

I['vinyl.range_size'] = format_text([[
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

I['vinyl.read_threads'] = format_text([[
    The maximum number of read threads that vinyl can use for concurrent
    operations, such as I/O and compression.
]])

I['vinyl.run_count_per_level'] = format_text([[
    The maximum number of runs per level in the vinyl LSM tree. If this
    number is exceeded, a new level is created. The `vinyl.run_count_per_level`
    setting is a default value for the run_count_per_level option passed to
    `space_object:create_index()`.
]])

I['vinyl.run_size_ratio'] = format_text([[
    The ratio between the sizes of different levels in the LSM tree.
    The `vinyl.run_size_ratio` setting is a default value for the
    run_size_ratio option passed to `space_object:create_index()`.
]])

I['vinyl.timeout'] = format_text([[
    The vinyl storage engine has a scheduler that performs compaction.
    When vinyl is low on available memory, the compaction scheduler may
    be unable to keep up with incoming update requests. In that situation,
    queries may time out after vinyl.timeout seconds. This should rarely occur,
    since normally vinyl throttles inserts when it is running low on compaction
    bandwidth. Compaction can also be initiated manually with
    `index_object:compact()`.
]])

I['vinyl.write_threads'] = format_text([[
    The maximum number of write threads that vinyl can use for some
    concurrent operations, such as I/O and compression.
]])

-- }}} vinyl configuration

-- {{{ quiver configuration

I['quiver'] = format_text([[
   This section defines configuration parameters related to
   the quiver storage engine.
]])

I['quiver.dir'] = format_text([[
    A directory where quiver files or subdirectories will be stored. This option
    may contain a relative file path. In this case, it is interpreted as
    relative to `process.work_dir`.
]])

I['quiver.memory'] = format_text([[
    The maximum size of in-memory buffers used for accumulating write requests.
    The quiver engine decides when it should start dumping in-memory buffers to
    disk depending on this parameter.
]])

I['quiver.run_size'] = format_text([[
    The maximum size of a run file, in bytes. When the quiver engine dumps
    in-memory buffers to disk, it splits the output stream into files depending
    on this parameter.
]])

-- }}} quiver configuration

-- {{{ wal configuration

I['wal'] = format_text([[
    This section defines configuration parameters related to write-ahead log.
]])

I['wal.cleanup_delay'] = format_text([[
    The delay in seconds used to prevent the Tarantool garbage collector from
    immediately removing write-ahead log files after a node restart. This
    delay eliminates possible erroneous situations when the master deletes
    WALs needed by replicas after restart. As a consequence, replicas sync
    with the master faster after its restart and don't need to download all
    the data again. Once all the nodes in the replica set are up and
    running, a scheduled garbage collection is started again even if
    `wal.cleanup_delay` has not expired.
]])

I['wal.dir'] = format_text([[
    A directory where write-ahead log (`.xlog`) files are stored. A relative
    path in this option is interpreted as relative to `process.work_dir`.

    By default, WAL files and snapshots are stored in the same directory.
    However, you can set different values for the `wal.dir` and `snapshot.dir`
    options to store them on different physical disks for performance matters.
]])

I['wal.dir_rescan_delay'] = format_text([[
    The time interval in seconds between periodic scans of the write-ahead-log
    file directory, when checking for changes to write-ahead-log files for the
    sake of replication or hot standby.
]])

I['wal.ext'] = format_text([[
    This section describes options related to WAL extensions.
]])

I['wal.ext.new'] = format_text([[
    Enable storing a new tuple for each CRUD operation performed. The option
    is in effect for all spaces. To adjust the option for specific spaces,
    use the `wal.ext.spaces` option.
]])

I['wal.ext.old'] = format_text([[
    Enable storing an old tuple for each CRUD operation performed. The option
    is in effect for all spaces. To adjust the option for specific spaces,
    use the `wal.ext.spaces` option.
]])

I['wal.ext.spaces'] = format_text([[
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

I['wal.ext.spaces.*'] = 'Per-space WAL extensions configuration.'

I['wal.ext.spaces.*.new'] = I['wal.ext.new']

I['wal.ext.spaces.*.old'] = I['wal.ext.old']

I['wal.max_size'] = format_text([[
    The maximum number of bytes in a single write-ahead log file. When a
    request would cause an `.xlog` file to become larger than `wal.max_size`,
    Tarantool creates a new WAL file.
]])

I['wal.mode'] = format_text([[
    Specify fiber-WAL-disk synchronization mode as:

    - `none`: write-ahead log is not maintained. A node with
      `wal.mode` set to `none` can't be a replication master.
    - `write`: fibers wait for their data to be written to the
      write-ahead log (no `fsync(2)`).
    - `fsync`: fibers wait for their data, `fsync(2)` follows each `write(2)`.
]])

I['wal.queue_max_size'] = format_text([[
    The size of the queue in bytes used by a replica to submit new transactions
    to a write-ahead log (WAL). This option helps limit the rate at which a
    replica submits transactions to the WAL. Limiting the queue size might be
    useful when a replica is trying to sync with a master and reads new
    transactions faster than writing them to the WAL.
]])

I['wal.retention_period'] = format_text([[
    The delay in seconds used to prevent the Tarantool garbage collector from
    removing a write-ahead log file after it has been closed. If a node is
    restarted, `wal.retention_period` counts down from the last modification
    time of the write-ahead log file.

    The garbage collector doesn't track write-ahead logs that are to be
    relayed to anonymous replicas, such as:

    - Anonymous replicas added as a part of a cluster configuration
      (see `replication.anon`).
    - CDC (Change Data Capture) that retrieves data using anonymous replication.

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

-- }}} Instance descriptions

-- {{{ Cluster descriptions

local C = {}

C[''] = 'Cluster configuration'

-- {{{ conditional configuration

C['conditional'] = format_text([[
    The `conditional` section defines the configuration parts that apply to
    instances that meet certain conditions.
]])

C['conditional.*'] = format_text([[
    Contains a part of the cluster configuration that is applied only if
    the specified conditions are met. Usually used to apply options that
    exist only on particular tarantool versions.

    The fields in this mapping are the same as in the cluster configuration,
    except:

    - The special `if` field holds the condition.

      Conditions can include `tarantool_version`, three-digit version
      literals (`3.2.1`) and support comparison operators (`>`, `<`, `>=`,
      `<=`, `==`, `!=`), as well as logical operators (`||`, `&&`) and
      parentheses for grouping.

    - Has no `conditional` section.
]])

C['conditional.*.*'] = format_text([[
    `if` or a cluster configuration field.
]])

-- }}} conditional configuration

-- {{{ groups configuration

C['groups'] = format_text([[
    This section provides the ability to define the full topology
    of a Tarantool cluster.
]])

C['groups.*'] = format_text([[
    A group of replicasets.

    The following rules are applied to group names:

    - The maximum number of symbols is 63.
    - Should start with a letter.
    - Can contain lowercase letters (a-z).
    - Can contain digits (0-9).
    - Can contain the following characters: -, _.
]])

C['groups.*.replicasets'] = 'Replica sets that belong to this group.'

C['groups.*.replicasets.*'] = format_text([[
    A replica set definition.

    Note that the rules applied to a replica set name are the same as for
    groups. Learn more in `groups.<group_name>`.
]])

C['groups.*.replicasets.*.instances'] = format_text([[
    Instances that belong to this replica set.
]])

C['groups.*.replicasets.*.instances.*'] = 'An instance definition.'

C['groups.*.replicasets.*.leader'] = format_text([[
    A replica set leader. This option can be used to set a replica set leader
    when manual `replication.failover` is used.

    To perform controlled failover, `<replicaset_name>.leader` can be
    temporarily removed or set to null.
]])

C['groups.*.replicasets.*.bootstrap_leader'] = format_text([[
    A bootstrap leader for a replica set. To specify a bootstrap leader
    manually, you need to set `replication.bootstrap_strategy` to `config`.
]])

-- }}} groups configuration

-- }}} Cluster descriptions

return {
    instance_descriptions = I,
    cluster_descriptions = C,
}
