-- net_box.lua (internal file)
local log      = require('log')
local ffi      = require('ffi')
local fiber    = require('fiber')
local msgpack  = require('msgpack')
local urilib   = require('uri')
local internal = require('net.box.lib')
local trigger  = require('internal.trigger')

local this_module

local max               = math.max
local fiber_clock       = fiber.clock

local check_select_opts   = box.internal.check_select_opts
local check_index_arg     = box.internal.check_index_arg
local check_space_arg     = box.internal.check_space_arg
local check_primary_index = box.internal.check_primary_index
local check_param_table   = box.internal.check_param_table

local ibuf_t = ffi.typeof('struct ibuf')

local TIMEOUT_INFINITY = 500 * 365 * 86400

-- select errors from box.error
local E_NO_CONNECTION        = box.error.NO_CONNECTION
local E_PROC_LUA             = box.error.PROC_LUA

-- Method types used internally by net.box.
local M_PING        = 0
local M_CALL_16     = 1
local M_CALL_17     = 2
local M_EVAL        = 3
local M_INSERT      = 4
local M_REPLACE     = 5
local M_DELETE      = 6
local M_UPDATE      = 7
local M_UPSERT      = 8
local M_SELECT      = 9
local M_EXECUTE     = 10
local M_PREPARE     = 11
local M_UNPREPARE   = 12
local M_GET         = 13
local M_MIN         = 14
local M_MAX         = 15
local M_COUNT       = 16
local M_BEGIN       = 17
local M_COMMIT      = 18
local M_ROLLBACK    = 19
-- Injects raw data into connection. Used by tests.
local M_INJECT      = 20

-- IPROTO feature id -> name
local IPROTO_FEATURE_NAMES = {
    [0]     = 'streams',
    [1]     = 'transactions',
    [2]     = 'error_extension',
    [3]     = 'watchers',
}

local REQUEST_OPTION_TYPES = {
    is_async    = "boolean",
    iterator    = "string",
    limit       = "number",
    offset      = "number",
    on_push     = "function",
    on_push_ctx = "any",
    return_raw  = "boolean",
    skip_header = "boolean",
    timeout     = "number",
    buffer = function(buf)
       if not ffi.istype(ibuf_t, buf) then
           return false, "struct ibuf"
       end
       return true
    end,
}

local CONNECT_OPTION_TYPES = {
    user                        = "string",
    password                    = "string",
    wait_connected              = "number, boolean",
    reconnect_after             = "number",
    call_16                     = "boolean",
    console                     = "boolean",
    connect_timeout             = "number",
    fetch_schema                = "boolean",
    required_protocol_version   = "number",
    required_protocol_features  = "table",
    _disable_graceful_shutdown  = "boolean",
}

-- Given an array of IPROTO feature ids, returns a map {feature_name: bool}.
local function iproto_features_resolve(feature_ids)
    local features = {}
    for _, feature_name in pairs(IPROTO_FEATURE_NAMES) do
        features[feature_name] = false
    end
    for _, feature_id in ipairs(feature_ids) do
        local feature_name = IPROTO_FEATURE_NAMES[feature_id]
        assert(feature_name ~= nil)
        features[feature_name] = true
    end
    return features
end

-- Check if all required features (array) are set in the features map.
-- Returns the status and an array of missing features.
local function iproto_features_check(features, required)
    local ok = true
    local missing = {}
    for _, feature_name in ipairs(required) do
        if not features[feature_name] then
            table.insert(missing, feature_name)
            ok = false
        end
    end
    return ok, missing
end

--
-- Default action on push during a synchronous request -
-- ignore.
--
local function on_push_sync_default() end

local function parse_connect_params(host_or_uri, ...) -- self? host_or_uri port? opts?
    local port, opts = ...
    if host_or_uri == this_module then host_or_uri, port, opts = ... end
    if type(port) == 'table' then opts = port; port = nil end
    if opts == nil then opts = {} else
        local copy = {}
        for k, v in pairs(opts) do copy[k] = v end
        opts = copy
    end
    local uri
    if port == nil and (type(host_or_uri) == 'string' or
                        type(host_or_uri) == 'number' or
                        type(host_or_uri) == 'table') then
        uri = host_or_uri
    elseif type(host_or_uri) == 'string' and (type(port) == 'string' or
                                              type(port) == 'number') then
        uri = urilib.format({host = host_or_uri, service = tostring(port)})
    else
        box.error(E_PROC_LUA,
                  "usage: connect(uri[, opts] | host, port[, opts])")
    end
    return uri, opts
end

local function remote_serialize(self)
    return {
        host = self.host,
        port = self.port,
        opts = next(self.opts) and self.opts,
        state = self.state,
        error = self.error,
        protocol = self.protocol,
        schema_version = self.schema_version,
        peer_uuid = self.peer_uuid,
        peer_version_id = self.peer_version_id,
        peer_protocol_version = self.peer_protocol_version,
        peer_protocol_features = self.peer_protocol_features,
    }
end

local function stream_serialize(self)
    local t = remote_serialize(self._conn)
    t.stream_id = self._stream_id
    return t
end

local function stream_spaces_serialize(self)
    return self._stream._conn.space
end

local function stream_space_serialize(self)
    return self._src
end

local function stream_indexes_serialize(self)
    return self._space._src.index
end

local function stream_index_serialize(self)
    return self._src
end

local remote_methods = {}
local remote_mt = {
    __index = remote_methods, __serialize = remote_serialize,
    __metatable = false
}

-- Create stream space index, which is same as connection space
-- index, but have non zero stream ID.
local function stream_wrap_index(stream_id, src)
    return setmetatable({
        _stream_id = stream_id,
        _src = src,
    }, {
        __index = src,
        __serialize = stream_index_serialize,
        __autocomplete = stream_index_serialize
    })
end

-- Metatable for stream space indexes. When stream space being
-- created there are no indexes in it. When accessing the space
-- index, we look for corresponding space index in corresponding
-- connection space. If it is found we create same index for the
-- stream space but with corresponding stream ID. We do not need
-- to compare stream _schema_version and connection schema_version,
-- because all access to index  is carried out through it's space.
-- So we update schema_version when we access space.
local stream_indexes_mt = {
    __index = function(self, key)
        local _space = self._space
        local src = _space._src.index[key]
        if not src then
            return nil
        end
        local res = stream_wrap_index(_space._stream_id, src)
        self[key] = res
        return res
    end,
    __serialize = stream_indexes_serialize,
    __autocomplete = stream_indexes_serialize
}

-- Create stream space, which is same as connection space,
-- but have non zero stream ID.
local function stream_wrap_space(stream, src)
    local res = setmetatable({
        _stream_id = stream._stream_id,
        _src = src,
        index = setmetatable({
            _space = nil,
        }, stream_indexes_mt)
    }, {
        __index = src,
        __serialize = stream_space_serialize,
        __autocomplete = stream_space_serialize
    })
    res.index._space = res
    return res
end

-- Metatable for stream spaces. When stream being created there
-- are no spaces in it. When user try to access some space in
-- stream, we first of all compare _schema_version of stream with
-- schema_version from connection and if they are not equal, we
-- clear stream space cache and update it's schema_version. Then
-- we look for corresponding space in the connection. If it is
-- found we create same space for the stream but with corresponding
-- stream ID.
local stream_spaces_mt = {
    __index = function(self, key)
        local stream = self._stream
        if stream._schema_version ~= stream._conn.schema_version then
            stream._schema_version = stream._conn.schema_version
            self._stream_space_cache = {}
        end
        if self._stream_space_cache[key] then
            return self._stream_space_cache[key]
        end
        local src = stream._conn.space[key]
        if not src then
            return nil
        end
        local res = stream_wrap_space(stream, src)
        self._stream_space_cache[key] = res
        return res
    end,
    __serialize = stream_spaces_serialize,
    __autocomplete = stream_spaces_serialize
}

-- This callback is invoked in a new fiber upon receiving 'box.shutdown' event
-- from a remote host. It runs on_shutdown triggers and then gracefully
-- terminates the connection.
local function graceful_shutdown(remote)
    remote._shutdown_pending = true
    local ok, err = pcall(remote._on_shutdown.run, remote._on_shutdown, remote)
    if not ok then
        log.error(err)
    end
    -- While the triggers were running, the connection could have been closed
    -- and even reestablished (if reconnect_after is set), in which case we
    -- must not initiate a graceful shutdown.
    if remote._shutdown_pending then
        remote._transport:graceful_shutdown()
        remote._shutdown_pending = nil
    end
end

local space_metatable, index_metatable

local function new_sm(uri, opts)
    local parsed_uri = urilib.parse(uri)
    if not parsed_uri then
        box.error()
    end
    if opts.user == nil and opts.password == nil then
        opts.user, opts.password = parsed_uri.login, parsed_uri.password
    end
    local host, port = parsed_uri.host, parsed_uri.service
    local user, password = opts.user, opts.password; opts.password = nil
    local last_reconnect_error
    local remote = {host = host, port = port, opts = opts, state = 'initial'}
    local function callback(what, ...)
        if remote._fiber == nil then
            remote._fiber = fiber.self()
        end
        if what == 'state_changed' then
            local state, err = ...
            local was_connected = remote._is_connected
            if state == 'active' then
                if not was_connected then
                    remote._is_connected = true
                    remote._on_connect:run(remote)
                end
            elseif state == 'error' or state == 'error_reconnect' or
                   state == 'closed' then
                if was_connected then
                    remote._is_connected = false
                    remote._on_disconnect:run(remote)
                end
                -- A server may exit after initiating a graceful shutdown but
                -- before all clients close their connections (for example, on
                -- timeout). In this case it's important that we don't initiate
                -- a graceful shutdown on our side, because a connection can
                -- already be reestablished (if reconnect_after is set) by the
                -- time we finish running on_shutdown triggers.
                remote._shutdown_pending = nil
            end
            remote.state, remote.error = state, err
            if state == 'error_reconnect' then
                -- Repeat the same error in verbose log only.
                -- Else the error clogs the log. See gh-3175.
                if err ~= last_reconnect_error then
                    log.warn('%s:%s: %s', host or "", port or "", err)
                    last_reconnect_error = err
                else
                    log.verbose('%s:%s: %s', host or "", port or "", err)
                end
            end
            remote._state_cond:broadcast()
        elseif what == 'handshake' then
            local greeting, version, features = ...
            remote.protocol = greeting.protocol
            remote.peer_uuid = greeting.uuid
            remote.peer_version_id = greeting.version_id
            features = iproto_features_resolve(features)
            remote.peer_protocol_version = version
            remote.peer_protocol_features = features
            if opts.required_protocol_version and
               opts.required_protocol_version > version then
                box.error({
                    code = E_NO_CONNECTION,
                    reason = string.format(
                        'Protocol version (%d) < required (%d)',
                         version, opts.required_protocol_version),
                })
            end
            if opts.required_protocol_features then
                local ok, missing = iproto_features_check(
                    features, opts.required_protocol_features)
                if not ok then
                    box.error({
                        code = E_NO_CONNECTION,
                        reason = 'Missing required protocol features: ' ..
                                 table.concat(missing, ', '),
                    })
                end
            end
        elseif what == 'did_fetch_schema' then
            remote:_install_schema(...)
        elseif what == 'event' then
            local key, value = ...
            local state = remote._watchers and remote._watchers[key]
            if state then
                state.value = value
                state.has_value = true
                state.version = state.version + 1
                state.is_acknowledged = false
                while state.idle do
                    local watcher = state.idle
                    state.idle = watcher._next
                    watcher:_run_async()
                end
            end
        end
    end
    if opts.console then
        box.error(box.error.ILLEGAL_PARAMS,
                  "Netbox text protocol support was dropped, " ..
                  "please use require('console').connect() instead")
    else
        setmetatable(remote, remote_mt)
        remote._space_mt = space_metatable(remote)
        remote._index_mt = index_metatable(remote)
        if opts.call_16 then
            remote.call = remote.call_16
            remote.eval = remote.eval_16
        end
    end
    remote._on_schema_reload = trigger.new("on_schema_reload")
    remote._on_shutdown = trigger.new("on_shutdown")
    remote._on_disconnect = trigger.new("on_disconnect")
    remote._on_connect = trigger.new("on_connect")
    remote._is_connected = false
    -- Signaled when the state changes.
    remote._state_cond = fiber.cond()
    -- Last stream ID used for this connection.
    remote._last_stream_id = 0
    local weak_refs = setmetatable({callback = callback}, {__mode = 'v'})
    -- Create a transport, adding auto-stop-on-GC feature.
    -- The tricky part is the callback:
    --  * callback references the transport (indirectly);
    --  * worker fiber references the callback;
    --  * fibers are GC roots - i.e. transport is never GC-ed!
    -- We solve the issue by making the worker->callback ref weak.
    -- Now it is necessary to have a strong ref to callback somewhere or
    -- it is GC-ed prematurely. We store a strong reference in the remote
    -- connection object.
    local function weak_callback(...)
        local callback = weak_refs.callback
        if callback then return callback(...) end
        -- The callback is responsible for handling graceful shutdown.
        -- If it's garbage collected, the connection won't be closed on
        -- receiving 'box.shutdown' event and so the server won't exit
        -- until the connection object is garbage collected, which may
        -- take forever. To avoid that, let's break the worker loop if
        -- we see that the callback is unavailable.
        weak_refs.transport:stop()
    end
    remote._callback = callback
    local transport = internal.new_transport(
            uri, user, password, weak_callback,
            opts.connect_timeout, opts.reconnect_after,
            opts.fetch_schema)
    weak_refs.transport = transport
    remote._transport = transport
    remote._gc_hook = ffi.gc(ffi.new('char[1]'), function()
        pcall(transport.stop, transport);
    end)
    if not opts._disable_graceful_shutdown then
        remote:watch('box.shutdown', function(_, value)
            if value then
                graceful_shutdown(remote)
            end
        end)
    end
    transport:start()
    if opts.wait_connected ~= false then
        remote:wait_state('active', tonumber(opts.wait_connected))
    end
    return remote
end

--
-- Connect to a remote server.
-- @param uri OR host and port. URI is a string like
--        hostname:port@login:password. Host and port can be
--        passed separately with login and password in the next
--        parameter.
-- @param opts Options like reconnect_after, connect_timeout,
--        wait_connected, login, password, ...
--
-- @retval Net.box object.
--
local function connect(...)
    local uri, opts = parse_connect_params(...)
    check_param_table(opts, CONNECT_OPTION_TYPES)
    return new_sm(uri, opts)
end

local function check_remote_arg(remote, method)
    if type(remote) ~= 'table' then
        local fmt = 'Use remote:%s(...) instead of remote.%s(...):'
        box.error(E_PROC_LUA, string.format(fmt, method, method))
    end
end

local function check_call_args(args)
    if args ~= nil and type(args) ~= 'table' and
       not msgpack.is_object(args) then
        error("Use remote:call(func_name, {arg1, arg2, ...}, opts) "..
              "instead of remote:call(func_name, arg1, arg2, ...)")
    end
end

local function check_eval_args(args)
    if args ~= nil and type(args) ~= 'table' and
       not msgpack.is_object(args) then
        error("Use remote:eval(expression, {arg1, arg2, ...}, opts) "..
              "instead of remote:eval(expression, arg1, arg2, ...)")
    end
end

local function stream_new_stream(stream)
    check_remote_arg(stream, 'new_stream')
    return stream._conn:new_stream()
end

local function stream_begin(stream, txn_opts, netbox_opts)
    check_remote_arg(stream, 'begin')
    check_param_table(netbox_opts, REQUEST_OPTION_TYPES)
    local timeout
    local txn_isolation
    if txn_opts then
        if type(txn_opts) ~= 'table' then
            error("txn_opts should be a table")
        end
        timeout = txn_opts.timeout
        if timeout and (type(timeout) ~= "number" or timeout <= 0) then
            error("timeout must be a number greater than 0")
        end
        txn_isolation = txn_opts.txn_isolation
        if txn_isolation ~= nil then
            txn_isolation =
                box.internal.normalize_txn_isolation_level(txn_isolation)
        end
    end
    local res = stream:_request(M_BEGIN, netbox_opts, nil,
                                stream._stream_id, timeout, txn_isolation)
    if netbox_opts and netbox_opts.is_async then
        return res
    end
end

local function stream_commit(stream, opts)
    check_remote_arg(stream, 'commit')
    check_param_table(opts, REQUEST_OPTION_TYPES)
    local res = stream:_request(M_COMMIT, opts, nil, stream._stream_id)
    if opts and opts.is_async then
        return res
    end
end

local function stream_rollback(stream, opts)
    check_remote_arg(stream, 'rollback')
    check_param_table(opts, REQUEST_OPTION_TYPES)
    local res = stream:_request(M_ROLLBACK, opts, nil, stream._stream_id)
    if opts and opts.is_async then
        return res
    end
end

function remote_methods:new_stream()
    check_remote_arg(self, 'new_stream')
    self._last_stream_id = self._last_stream_id + 1
    local stream = setmetatable({
        new_stream = stream_new_stream,
        begin = stream_begin,
        commit = stream_commit,
        rollback = stream_rollback,
        _stream_id = self._last_stream_id,
        space = setmetatable({
            _stream_space_cache = {},
            _stream = nil,
        }, stream_spaces_mt),
        _conn = self,
        _schema_version = self.schema_version,
    }, { __index = self, __serialize = stream_serialize })
    stream.space._stream = stream
    return stream
end

local watcher_methods = {}
local watcher_mt = {
    __index = watcher_methods,
    __tostring = function()
        return 'net.box.watcher'
    end,
}
watcher_mt.__serialize = watcher_mt.__tostring

function watcher_methods:_run()
    local state = self._state
    assert(state ~= nil)
    assert(state.has_value)
    self._version = state.version
    local status, err = pcall(self._func, state.key, state.value)
    if not status then
        log.error(err)
    end
    if not self._state then
        -- The watcher was unregistered while the callback was running.
        return
    end
    assert(state == self._state)
    if self._version == state.version then
        -- The value was not updated while this watcher was running.
        -- Append it to the list of ready watchers and send an ack to
        -- the server unless we've already sent it.
        self._next = state.idle
        state.idle = self
        if not state.is_acknowledged then
            self._conn._transport:watch(state.key)
            state.is_acknowledged = true
        end
    else
        -- The value was updated while this watcher was running.
        -- Rerun it with the new value.
        assert(self._version < state.version)
        return self:_run()
    end
end

function watcher_methods:_run_async()
    fiber.new(self._run, self)
end

function watcher_methods:unregister()
    if type(self) ~= 'table' then
        box.error(E_PROC_LUA,
                  'Use watcher:unregister() instead of watcher.unregister()')
    end
    local state = self._state
    if not self._state then
        box.error(E_PROC_LUA, 'Watcher is already unregistered')
    end
    self._state = nil
    local conn = self._conn
    assert(conn._watchers ~= nil)
    assert(conn._watchers[state.key] == state)
    if state.idle then
        -- Remove the watcher from the idle list.
        if state.idle == self then
            state.idle = self._next
        else
            local watcher = state.idle
            while watcher._next do
                if watcher._next == self then
                    watcher._next = self._next
                    break
                end
                watcher = watcher._next
            end
        end
    end
    assert(state.watcher_count > 0)
    state.watcher_count = state.watcher_count - 1
    if state.watcher_count == 0 then
        -- No watchers left. Unsubscribe and drop the state.
        conn._transport:unwatch(state.key)
        conn._watchers[state.key] = nil
    end
end

function remote_methods:watch(key, func)
    check_remote_arg(self, 'watch')
    if type(key) ~= 'string' then
        box.error(E_PROC_LUA, 'key must be a string')
    end
    if type(func) ~= 'function' then
        box.error(E_PROC_LUA, 'func must be a function')
    end
    if not self._watchers then
        self._watchers = {}
        -- Install a trigger to resubscribe registered watchers on reconnect.
        self._on_connect(function(conn)
            for key, _ in pairs(conn._watchers) do
                conn._transport:watch(key)
            end
        end)
    end
    local state = self._watchers[key]
    if not state then
        state = {}
        state.key = key
        state.value = nil
        -- Set when the first value is received for the state. We can't rely
        -- on the value being non-nil, because a state can actually have nil
        -- value so we need a separate flag.
        state.has_value = false
        -- Incremented every time a new value is received for this state.
        -- We use to reschedule a watcher that was already running when a new
        -- value was received.
        state.version = 0
        -- Set to true if the last received value has been acknowledged
        -- (that is we sent a WATCH packet after receiving it).
        state.is_acknowledged = false
        -- Number of watchers registered for this key. We delete a state when
        -- nobody uses it.
        state.watcher_count = 0
        -- Singly-linked (by ._next) list of watchers ready to be notified
        -- (i.e. registered and not currently running).
        state.idle = nil
        -- We don't care whether the connection is active or not, because if
        -- the connection fails, we will resubscribe all registered watchers
        -- from the on_connect trigger.
        self._transport:watch(key)
        self._watchers[key] = state
    end
    local watcher = setmetatable({
        _conn = self,
        _func = func,
        _state = state,
        _version = 0,
        _next = nil,
    }, watcher_mt)
    state.watcher_count = state.watcher_count + 1
    if state.has_value then
        watcher:_run_async()
    else
        watcher._next = state.idle
        state.idle = watcher
    end
    return watcher
end

function remote_methods:close()
    check_remote_arg(self, 'close')
    self._transport:stop(true)
end

function remote_methods:on_schema_reload(...)
    check_remote_arg(self, 'on_schema_reload')
    return self._on_schema_reload(...)
end

function remote_methods:on_shutdown(...)
    check_remote_arg(self, 'on_shutdown')
    return self._on_shutdown(...)
end

function remote_methods:on_disconnect(...)
    check_remote_arg(self, 'on_disconnect')
    return self._on_disconnect(...)
end

function remote_methods:on_connect(...)
    check_remote_arg(self, 'on_connect')
    return self._on_connect(...)
end

function remote_methods:is_connected()
    check_remote_arg(self, 'is_connected')
    return self.state == 'active' or self.state == 'fetch_schema'
end

function remote_methods:wait_connected(timeout)
    check_remote_arg(self, 'wait_connected')
    return self:wait_state('active', timeout)
end

--
-- Make a request, which throws an exception in case of critical errors
-- (e.g. wrong API usage) and returns nil,err if there's connection related
-- issues.
--
function remote_methods:_request_impl(method, opts, format, stream_id, ...)
    local transport = self._transport
    local on_push, on_push_ctx, buffer, skip_header, return_raw, deadline
    -- Extract options, set defaults, check if the request is
    -- async.
    if opts then
        buffer = opts.buffer
        skip_header = opts.skip_header
        return_raw = opts.return_raw
        if opts.is_async then
            if opts.on_push or opts.on_push_ctx then
                error('To handle pushes in an async request use future:pairs()')
            end
            return transport:perform_async_request(buffer, skip_header,
                                                   return_raw, table.insert,
                                                   {}, format, stream_id,
                                                   method, ...)
        end
        if opts.timeout then
            deadline = fiber_clock() + opts.timeout
        end
        on_push = opts.on_push or on_push_sync_default
        on_push_ctx = opts.on_push_ctx
    else
        on_push = on_push_sync_default
    end
    -- Execute synchronous request.
    if self._fiber == fiber.self() then
        error('Synchronous requests are not allowed in net.box trigger')
    end
    local timeout = deadline and max(0, deadline - fiber_clock())
    if self.state ~= 'active' then
        self:wait_state('active', timeout)
        timeout = deadline and max(0, deadline - fiber_clock())
    end
    local res, err = transport:perform_request(timeout, buffer, skip_header,
                                               return_raw, on_push, on_push_ctx,
                                               format, stream_id, method, ...)
    -- Try to wait until a schema is reloaded if needed.
    -- Regardless of reloading result, the main response is
    -- returned, since it does not depend on any schema things.
    if self.state == 'fetch_schema' then
        timeout = deadline and max(0, deadline - fiber_clock())
        self:wait_state('active', timeout)
    end
    return res, err
end

function remote_methods:_request(method, opts, format, stream_id, ...)
    local res, err = self:_request_impl(method, opts, format, stream_id, ...)
    if err then
        box.error(err)
    end
    return res
end

function remote_methods:_inject(str, opts)
    check_param_table(opts, REQUEST_OPTION_TYPES)
    return self:_request(M_INJECT, opts, nil, nil, str)
end

function remote_methods:_next_sync()
    return self._transport:next_sync()
end

function remote_methods:ping(opts)
    check_remote_arg(self, 'ping')
    check_param_table(opts, REQUEST_OPTION_TYPES)
    if opts and opts.is_async then
        error("conn:ping() doesn't support `is_async` argument")
    end
    local _, err = self:_request_impl(M_PING, opts, nil, self._stream_id)
    return err == nil
end

function remote_methods:reload_schema()
    check_remote_arg(self, 'reload_schema')
    self:ping()
end

-- @deprecated since 1.7.4
function remote_methods:call_16(func_name, ...)
    check_remote_arg(self, 'call')
    return (self:_request(M_CALL_16, nil, nil, self._stream_id,
                          tostring(func_name), {...}))
end

function remote_methods:call(func_name, args, opts)
    check_remote_arg(self, 'call')
    check_call_args(args)
    check_param_table(opts, REQUEST_OPTION_TYPES)
    args = args or {}
    local res = self:_request(M_CALL_17, opts, nil, self._stream_id,
                              tostring(func_name), args)
    if type(res) ~= 'table' or opts and opts.is_async then
        return res
    end
    return unpack(res)
end

-- @deprecated since 1.7.4
function remote_methods:eval_16(code, ...)
    check_remote_arg(self, 'eval')
    return unpack((self:_request(M_EVAL, nil, nil, self._stream_id,
                                 code, {...})))
end

function remote_methods:eval(code, args, opts)
    check_remote_arg(self, 'eval')
    check_eval_args(args)
    check_param_table(opts, REQUEST_OPTION_TYPES)
    args = args or {}
    local res = self:_request(M_EVAL, opts, nil, self._stream_id, code, args)
    if type(res) ~= 'table' or opts and opts.is_async then
        return res
    end
    return unpack(res)
end

function remote_methods:execute(query, parameters, sql_opts, netbox_opts)
    check_remote_arg(self, "execute")
    if sql_opts ~= nil then
        box.error(box.error.UNSUPPORTED, "execute", "options")
    end
    check_param_table(netbox_opts, REQUEST_OPTION_TYPES)
    return self:_request(M_EXECUTE, netbox_opts, nil, self._stream_id,
                         query, parameters or {}, sql_opts or {})
end

function remote_methods:prepare(query, parameters, sql_opts, netbox_opts) -- luacheck: no unused args
    check_remote_arg(self, "prepare")
    if type(query) ~= "string" then
        box.error(box.error.SQL_PREPARE, "expected string as SQL statement")
    end
    if sql_opts ~= nil then
        box.error(box.error.UNSUPPORTED, "prepare", "options")
    end
    check_param_table(netbox_opts, REQUEST_OPTION_TYPES)
    return self:_request(M_PREPARE, netbox_opts, nil, self._stream_id, query)
end

function remote_methods:unprepare(query, parameters, sql_opts, netbox_opts)
    check_remote_arg(self, "unprepare")
    if type(query) ~= "number" then
        box.error(box.error.ILLEGAL_PARAMS,
                  "query id is expected to be numeric")
    end
    if sql_opts ~= nil then
        box.error(box.error.UNSUPPORTED, "unprepare", "options")
    end
    check_param_table(netbox_opts, REQUEST_OPTION_TYPES)
    return self:_request(M_UNPREPARE, netbox_opts, nil, self._stream_id,
                         query, parameters or {}, sql_opts or {})
end

function remote_methods:wait_state(state, timeout)
    check_remote_arg(self, 'wait_state')
    local deadline = fiber_clock() + (timeout or TIMEOUT_INFINITY)
    -- FYI: [] on a string is valid
    repeat until self.state == state or state[self.state] or
                 self.state == 'closed' or self.state == 'error' or
                 (not self.opts.reconnect_after and
                  self.state == 'graceful_shutdown') or
                 not self._state_cond:wait(max(0, deadline - fiber_clock()))
    return self.state == state or state[self.state] or false
end

function remote_methods:_install_schema(schema_version, spaces, indices,
                                        collations)
    local sl, space_mt, index_mt = {}, self._space_mt, self._index_mt
    for _, space in pairs(spaces) do
        local name = space[3]
        local id = space[1]
        local engine = space[4]
        local field_count = space[5]
        local format = space[7] or {}
        local s = {}
        if self.space ~= nil and self.space[id] ~= nil then
            s = self.space[id]
        else
            setmetatable(s, space_mt)
        end
        s.id = id
        s.name = name
        s.engine = engine
        s.field_count = field_count
        s.enabled = true
        s.index = {}
        s.temporary = false
        s.is_sync = false
        s._format = format
        s._format_cdata = box.internal.new_tuple_format(format)
        s.connection = self
        if #space > 5 then
            local opts = space[6]
            if type(opts) == 'table' then
                -- Tarantool >= 1.7.0
                s.temporary = not not opts.temporary
                s.is_sync = not not opts.is_sync
            elseif type(opts) == 'string' then
                -- Tarantool < 1.7.0
                s.temporary = string.match(opts, 'temporary') ~= nil
            end
        end

        sl[id] = s
        sl[name] = s
    end

    for _, index in pairs(indices) do
        local idx = {
            space   = index[1],
            id      = index[2],
            name    = index[3],
            type    = string.upper(index[4]),
            parts   = {},
        }
        local OPTS = 5
        local PARTS = 6

        if type(index[OPTS]) == 'number' then
            idx.unique = index[OPTS] == 1

            for k = 0, index[PARTS] - 1 do
                local pktype = index[7 + k * 2 + 1]
                local pkfield = index[7 + k * 2]

                local pk = {
                    type = pktype,
                    fieldno = pkfield + 1
                }
                idx.parts[k + 1] = pk
            end
        else
            for k = 1, #index[PARTS] do
                local pknullable = index[PARTS][k].is_nullable or false
                local pkexcludenull = index[PARTS][k].exclude_null or false
                local pkcollationid = index[PARTS][k].collation
                local pkpath = index[PARTS][k].path
                local pktype = index[PARTS][k][2] or index[PARTS][k].type
                local pkfield = index[PARTS][k][1] or index[PARTS][k].field
                -- resolve a collation name if a peer has
                -- _vcollation view
                local pkcollation = nil
                if pkcollationid ~= nil and collations ~= nil then
                    pkcollation = collations[pkcollationid + 1][2]
                end

                local pk = {
                    type = pktype,
                    fieldno = pkfield + 1,
                    is_nullable = pknullable,
                    exclude_null = pkexcludenull,
                    path = pkpath,
                }
                if collations == nil then
                    pk.collation_id = pkcollationid
                else
                    pk.collation = pkcollation
                end
                idx.parts[k] = pk
            end
            idx.unique = not not index[OPTS].unique
        end

        if sl[idx.space] ~= nil then
            sl[idx.space].index[idx.id] = idx
            sl[idx.space].index[idx.name] = idx
            idx.space = sl[idx.space]
            setmetatable(idx, index_mt)
        end
    end

    self.schema_version = schema_version
    self.space = sl
    self._on_schema_reload:run(self)
end

local function nothing_or_data(value)
    if value ~= nil then
        return value
    end
end

space_metatable = function(remote)
    local methods = {}

    function methods:insert(tuple, opts)
        check_space_arg(self, 'insert')
        check_param_table(opts, REQUEST_OPTION_TYPES)
        return remote:_request(M_INSERT, opts, self._format_cdata,
                               self._stream_id, self.id, tuple)
    end

    function methods:replace(tuple, opts)
        check_space_arg(self, 'replace')
        check_param_table(opts, REQUEST_OPTION_TYPES)
        return remote:_request(M_REPLACE, opts, self._format_cdata,
                               self._stream_id, self.id, tuple)
    end

    function methods:select(key, opts)
        check_space_arg(self, 'select')
        return check_primary_index(self):select(key, opts)
    end

    function methods:delete(key, opts)
        check_space_arg(self, 'delete')
        return check_primary_index(self):delete(key, opts)
    end

    function methods:update(key, oplist, opts)
        check_space_arg(self, 'update')
        return check_primary_index(self):update(key, oplist, opts)
    end

    function methods:upsert(key, oplist, opts)
        check_space_arg(self, 'upsert')
        check_param_table(opts, REQUEST_OPTION_TYPES)
        return nothing_or_data(remote:_request(M_UPSERT, opts, nil,
                                               self._stream_id, self.id,
                                               key, oplist))
    end

    function methods:get(key, opts)
        check_space_arg(self, 'get')
        return check_primary_index(self):get(key, opts)
    end

    function methods:format(format)
        if format == nil then
            return self._format
        else
            box.error(box.error.UNSUPPORTED, "net.box", "setting space format")
        end
    end

    return { __index = methods, __metatable = false }
end

index_metatable = function(remote)
    local methods = {}

    function methods:select(key, opts)
        check_index_arg(self, 'select')
        check_param_table(opts, REQUEST_OPTION_TYPES)
        local key_is_nil = (key == nil or
                            (type(key) == 'table' and #key == 0))
        local iterator, offset, limit = check_select_opts(opts, key_is_nil)
        return (remote:_request(M_SELECT, opts, self.space._format_cdata,
                                self._stream_id, self.space.id, self.id,
                                iterator, offset, limit, key))
    end

    function methods:get(key, opts)
        check_index_arg(self, 'get')
        check_param_table(opts, REQUEST_OPTION_TYPES)
        if opts and opts.buffer then
            error("index:get() doesn't support `buffer` argument")
        end
        return nothing_or_data(remote:_request(M_GET, opts,
                                               self.space._format_cdata,
                                               self._stream_id,
                                               self.space.id, self.id,
                                               box.index.EQ, 0, 2, key))
    end

    function methods:min(key, opts)
        check_index_arg(self, 'min')
        check_param_table(opts, REQUEST_OPTION_TYPES)
        if opts and opts.buffer then
            error("index:min() doesn't support `buffer` argument")
        end
        return nothing_or_data(remote:_request(M_MIN, opts,
                                               self.space._format_cdata,
                                               self._stream_id,
                                               self.space.id, self.id,
                                               box.index.GE, 0, 1, key))
    end

    function methods:max(key, opts)
        check_index_arg(self, 'max')
        check_param_table(opts, REQUEST_OPTION_TYPES)
        if opts and opts.buffer then
            error("index:max() doesn't support `buffer` argument")
        end
        return nothing_or_data(remote:_request(M_MAX, opts,
                                               self.space._format_cdata,
                                               self._stream_id,
                                               self.space.id, self.id,
                                               box.index.LE, 0, 1, key))
    end

    function methods:count(key, opts)
        check_index_arg(self, 'count')
        check_param_table(opts, REQUEST_OPTION_TYPES)
        if opts and opts.buffer then
            error("index:count() doesn't support `buffer` argument")
        end
        local code = string.format('box.space.%s.index.%s:count',
                                   self.space.name, self.name)
        return remote:_request(M_COUNT, opts, nil, self._stream_id,
                               code, { key, opts })
    end

    function methods:delete(key, opts)
        check_index_arg(self, 'delete')
        check_param_table(opts, REQUEST_OPTION_TYPES)
        return nothing_or_data(remote:_request(M_DELETE, opts,
                                               self.space._format_cdata,
                                               self._stream_id, self.space.id,
                                               self.id, key))
    end

    function methods:update(key, oplist, opts)
        check_index_arg(self, 'update')
        check_param_table(opts, REQUEST_OPTION_TYPES)
        return nothing_or_data(remote:_request(M_UPDATE, opts,
                                               self.space._format_cdata,
                                               self._stream_id, self.space.id,
                                               self.id, key, oplist))
    end

    return { __index = methods, __metatable = false }
end

this_module = {
    connect = connect,
    new = connect, -- Tarantool < 1.7.1 compatibility,
    _method = { -- for tests
        ping        = M_PING,
        call_16     = M_CALL_16,
        call_17     = M_CALL_17,
        eval        = M_EVAL,
        insert      = M_INSERT,
        replace     = M_REPLACE,
        delete      = M_DELETE,
        update      = M_UPDATE,
        upsert      = M_UPSERT,
        select      = M_SELECT,
        execute     = M_EXECUTE,
        prepare     = M_PREPARE,
        unprepare   = M_UNPREPARE,
        get         = M_GET,
        min         = M_MIN,
        max         = M_MAX,
        count       = M_COUNT,
        begin       = M_BEGIN,
        commit      = M_COMMIT,
        rollback    = M_ROLLBACK,
        inject      = M_INJECT,
    }
}

function this_module.timeout(timeout, ...)
    if type(timeout) == 'table' then timeout = ... end
    if not timeout then return this_module end
    local function timed_connect(...)
        local uri, opts = parse_connect_params(...)
        if opts.wait_connected ~= false then opts.wait_connected = timeout end
        return connect(uri, opts)
    end
    return setmetatable({
        connect = timed_connect, new = timed_connect
    }, {__index = this_module})
end

local function rollback()
    if rawget(box, 'rollback') ~= nil then
        -- roll back local transaction on error
        box.rollback()
    end
end

local function handle_eval_result(status, ...)
    if not status then
        rollback()
        return box.error(E_PROC_LUA, (...))
    end
    local results = {...}
    for i = 1, select('#', ...) do
        if type(results[i]) == 'cdata' then
            results[i] = msgpack.decode(msgpack.encode(results[i]))
        end
    end
    return unpack(results)
end

this_module.self = {
    ping = function() return true end,
    reload_schema = function() end,
    close = function() end,
    timeout = function(self) return self end,
    wait_connected = function(self) return true end,
    is_connected = function(self) return true end,
    call = function(__box, proc_name, args)
        check_remote_arg(__box, 'call')
        check_call_args(args)
        args = args or {}
        proc_name = tostring(proc_name)
        local status, proc, obj = pcall(package.loaded['box.internal'].
            call_loadproc, proc_name)
        if not status then
            rollback()
            return box.error() -- re-throw
        end
        if obj ~= nil then
            return handle_eval_result(pcall(proc, obj, unpack(args)))
        else
            return handle_eval_result(pcall(proc, unpack(args)))
        end
    end,
    eval = function(__box, expr, args)
        check_remote_arg(__box, 'eval')
        check_eval_args(args)
        args = args or {}
        local proc, errmsg = loadstring(expr)
        if not proc then
            proc, errmsg = loadstring("return "..expr)
        end
        if not proc then
            rollback()
            return box.error(box.error.PROC_LUA, errmsg)
        end
        return handle_eval_result(pcall(proc, unpack(args)))
    end
}

setmetatable(this_module.self, {
    __index = function(self, key)
        if key == 'space' then
            -- proxy self.space to box.space
            return require('box').space
        end
        return nil
    end
})

package.loaded['net.box'] = this_module
