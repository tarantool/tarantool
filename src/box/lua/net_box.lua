-- net_box.lua (internal file)
local log      = require('log')
local ffi      = require('ffi')
local buffer   = require('buffer')
local socket   = require('socket')
local fiber    = require('fiber')
local msgpack  = require('msgpack')
local errno    = require('errno')
local urilib   = require('uri')
local internal = require('net.box.lib')
local trigger  = require('internal.trigger')

local band          = bit.band
local max           = math.max
local fiber_time    = fiber.time
local fiber_self    = fiber.self
local ibuf_decode   = msgpack.ibuf_decode

local table_new           = require('table.new')
local check_iterator_type = box.internal.check_iterator_type

local communicate     = internal.communicate
local encode_auth     = internal.encode_auth
local encode_select   = internal.encode_select
local decode_greeting = internal.decode_greeting

local sequence_mt      = { __serialize = 'sequence' }
local TIMEOUT_INFINITY = 500 * 365 * 86400
local VSPACE_ID        = 281
local VINDEX_ID        = 289

local IPROTO_STATUS_KEY    = 0x00
local IPROTO_ERRNO_MASK    = 0x7FFF
local IPROTO_SYNC_KEY      = 0x01
local IPROTO_SCHEMA_ID_KEY = 0x05
local IPROTO_DATA_KEY      = 0x30
local IPROTO_ERROR_KEY     = 0x31
local IPROTO_GREETING_SIZE = 128

-- select errors from box.error
local E_UNKNOWN              = box.error.UNKNOWN
local E_NO_CONNECTION        = box.error.NO_CONNECTION
local E_TIMEOUT              = box.error.TIMEOUT
local E_WRONG_SCHEMA_VERSION = box.error.WRONG_SCHEMA_VERSION
local E_PROC_LUA             = box.error.PROC_LUA

-- utility tables
local is_final_state         = {closed = 1, error = 1}
local method_codec           = {
    ping    = internal.encode_ping,
    call_16 = internal.encode_call_16,
    call_17 = internal.encode_call,
    eval    = internal.encode_eval,
    insert  = internal.encode_insert,
    replace = internal.encode_replace,
    delete  = internal.encode_delete,
    update  = internal.encode_update,
    upsert  = internal.encode_upsert,
    select  = function(buf, id, schema_id, spaceno, indexno, key, opts)
        if type(spaceno) ~= 'number' then
            box.error(box.error.NO_SUCH_SPACE, '#'..tostring(spaceno))
        end

        if type(indexno) ~= 'number' then
            box.error(box.error.NO_SUCH_INDEX, indexno, '#'..tostring(indexno))
        end

        local limit = tonumber(opts and opts.limit) or 0xFFFFFFFF
        local offset = tonumber(opts and opts.offset) or 0

        local key_is_nil = (key == nil or
                            (type(key) == 'table' and #key == 0))
        encode_select(buf, id, schema_id, spaceno, indexno,
                      check_iterator_type(opts, key_is_nil),
                      offset, limit, key)
    end,
    -- inject raw data into connection, used by console and tests
    inject = function(buf, id, schema_id, bytes)
        local ptr = buf:reserve(#bytes)
        ffi.copy(ptr, bytes, #bytes)
        buf.wpos = ptr + #bytes
    end
}

local function next_id(id) return band(id + 1, 0x7FFFFFFF) end

-- function create_transport(host, port, user, password, callback)
--
-- Transport methods: connect(), close(), perfrom_request(), wait_state()
--
-- Basically, *transport* is a TCP connection speaking one of
-- Tarantool network protocols. This is a low-level interface.
-- Primary features:
--  * implements protocols; concurrent perform_request()-s benefit from
--    multiplexing support in the protocol;
--  * schema-aware (optional) - snoops responses and initiates
--    schema reload when a request fails due to schema version mismatch;
--  * delivers transport events via the callback.
--
-- Transport state machine:
--
-- State machine starts in 'initial' state. Connect method changes
-- the state to 'connecting' and spawns a worker fiber. Close
-- method sets the state to 'closed' and kills the worker.
-- If the transport is already in 'error' state close() does nothing.
--
-- State chart:
--
--  initial -> connecting -> active
--                        \
--                          -> auth -> fetch_schema <-> active
--
--  (any state, on error) -> error_reconnect -> connecting -> ...
--                                           \
--                                             -> [error]
--  (any_state, but [error]) -> [closed]
--
--
-- State change events can be delivered to the transport user via
-- the optional 'callback' argument:
--
-- The callback functions needs to have the following signature:
--
--  callback(event_name, ...)
--
-- The following events are delivered, with arguments:
--
--  'state_changed', state, errno, error
--  'handshake', greeting           -> nil (accept) / errno, error (reject)
--  'will_fetch_schema'             -> true (approve) / false (skip fetch)
--  'did_fetch_schema', schema_id, spaces, indices
--  'will_reconnect', errno, error  -> true (approve) / false (reject)
--
-- Suggestion for callback writers: sleep a few secs before approving
-- reconnect.
--
local function create_transport(host, port, user, password, callback)
    -- check / normalize credentials
    if user == nil and password ~= nil then
        box.error(E_PROC_LUA, 'net.box: user is not defined')
    end
    if user ~= nil and password == nil then password = '' end

    -- state: current state, only the worker fiber and connect method
    -- change state
    local state            = 'initial'
    local last_errno
    local last_error
    local state_cond       = fiber.cond() -- signaled when the state changes

    -- requests: requests currently 'in flight', keyed by a request id;
    -- value refs are weak hence if a client dies unexpectedly,
    -- GC cleans the mess. Client submits a request and waits on state_cond.
    -- If the reponse arrives within the timeout, the worker wakes
    -- client fiber explicitly. Otherwize, wait on state_cond completes and
    -- the client reports E_TIMEOUT.
    local requests         = setmetatable({}, { __mode = 'v' })
    local next_request_id  = 1

    local worker_fiber
    local connection
    local send_buf         = buffer.ibuf(buffer.READAHEAD)
    local recv_buf         = buffer.ibuf(buffer.READAHEAD)

    -- STATE SWITCHING --
    local function set_state(new_state, new_errno, new_error, schema_id)
        state = new_state
        last_errno = new_errno
        last_error = new_error
        callback('state_changed', new_state, new_errno, new_error)
        state_cond:broadcast()
        if state ~= 'active' then
            -- cancel all requests but the ones bearing the particular
            -- schema id; if schema id was omitted or we aren't fetching
            -- schema, cancel everything
            if not schema_id or state ~= 'fetch_schema' then
                schema_id = -1
            end
            local next_id, next_request = next(requests)
            while next_id do
                local id, request = next_id, next_request
                next_id, next_request = next(requests, id)
                if request.schema_id ~= schema_id then
                    requests[id] = nil -- this marks the request as completed
                    request.errno  = new_errno
                    request.response = new_error
                end
            end
        end
    end

    -- FYI: [] on a string is valid
    local function wait_state(target_state, timeout)
        local deadline = fiber_time() + (timeout or TIMEOUT_INFINITY)
        repeat until state == target_state or target_state[state] or
                     is_final_state[state] or
                     not state_cond:wait(max(0, deadline - fiber_time()))
        return state == target_state or target_state[state] or false
    end

    -- CONNECT/CLOSE --
    local protocol_sm

    local function connect()
        if state ~= 'initial' then return not is_final_state[state] end
        set_state('connecting')
        fiber.create(function()
            worker_fiber = fiber_self()
            fiber.name(string.format('%s:%s (net.box)', host, port))
            local ok, err = pcall(protocol_sm)
            if not (ok or is_final_state[state]) then
                set_state('error', E_UNKNOWN, err)
            end
            if connection then
                connection:close()
                connection = nil
            end
            send_buf:recycle()
            recv_buf:recycle()
            worker_fiber = nil
        end)
        return true
    end

    local function close()
        if not is_final_state[state] then
            set_state('closed', E_NO_CONNECTION, 'Connection closed')
        end
        if worker_fiber then
            worker_fiber:cancel()
            worker_fiber = nil
        end
    end

    -- REQUEST/RESPONSE --
    local function perform_request(timeout, method, schema_id, ...)
        if state ~= 'active' then
            return last_errno or E_NO_CONNECTION, last_error
        end
        local deadline = fiber_time() + (timeout or TIMEOUT_INFINITY)
        -- alert worker to notify it of the queued outgoing data;
        -- if the buffer wasn't empty, assume the worker was already alerted
        if send_buf:size() == 0 then
            worker_fiber:wakeup()
        end
        local id = next_request_id
        method_codec[method](send_buf, id, schema_id, ...)
        next_request_id = next_id(id)
        local request = table_new(0, 5) -- reserve space for 5 keys
        request.client = fiber_self()
        request.method = method
        request.schema_id = schema_id
        requests[id] = request
        repeat
            local timeout = max(0, deadline - fiber_time())
            if not state_cond:wait(timeout) then
                requests[id] = nil
                return E_TIMEOUT, 'Timeout exceeded'
            end
        until requests[id] == nil -- i.e. completed (beware spurious wakeups)
        return request.errno, request.response
    end

    local function dispatch_response(id, errno, response)
        local request = requests[id]
        if request then -- someone is waiting for the response
            requests[id] = nil
            request.errno, request.response = errno, response
            local client = request.client
            if client:status() ~= 'dead' then client:wakeup() end
        end
    end

    local function dispatch_response_iproto(hdr, body)
        local id = hdr[IPROTO_SYNC_KEY]
        local status = hdr[IPROTO_STATUS_KEY]
        if status ~= 0 then
            return dispatch_response(id, band(status, IPROTO_ERRNO_MASK),
                                     body[IPROTO_ERROR_KEY])
        else
            return dispatch_response(id, nil, body[IPROTO_DATA_KEY])
        end
    end

    local function new_request_id()
        local id = next_request_id;
        next_request_id = next_id(id)
        return id
    end

    -- IO (WORKER FIBER) --
    local function send_and_recv(limit_or_boundary, timeout)
        return communicate(connection:fd(), send_buf, recv_buf,
                           limit_or_boundary, timeout)
    end

    local function send_and_recv_iproto(timeout)
        local data_len = recv_buf.wpos - recv_buf.rpos
        local required = 0
        if data_len < 5 then
            required = 5
        else
            -- PWN! insufficient input validation
            local rpos, len = ibuf_decode(recv_buf.rpos)
            required = (rpos - recv_buf.rpos) + len
            if data_len >= required then
                local hdr
                rpos, hdr = ibuf_decode(rpos)
                local body = {}
                if rpos - recv_buf.rpos < required then
                    rpos, body = ibuf_decode(rpos)
                end
                recv_buf.rpos = rpos
                return nil, hdr, body
            end
        end
        local deadline = fiber_time() + (timeout or TIMEOUT_INFINITY)
        local err, extra = send_and_recv(required, timeout)
        if err then
            return err, extra
        end
        return send_and_recv_iproto(max(0, deadline - fiber_time()))
    end

    local function send_and_recv_console(timeout)
        local delim = '\n...\n'
        local err, delim_pos = send_and_recv(delim, timeout)
        if err then
            return err, delim_pos
        else
            local response = ffi.string(recv_buf.rpos, delim_pos + #delim)
            recv_buf.rpos = recv_buf.rpos + delim_pos + #delim
            return nil, response
        end
    end

    -- PROTOCOL STATE MACHINE (WORKER FIBER) --
    --
    -- The sm is implemented as a collection of functions performing
    -- tail-recursive calls to each other. Yep, Lua optimizes
    -- such calls, and yep, this is the canonical way to implement
    -- a state machine in Lua.
    local console_sm, iproto_auth_sm, iproto_schema_sm, iproto_sm, error_sm

    protocol_sm = function ()
        local tm_begin, tm = fiber.time(), callback('fetch_connect_timeout')
        connection = socket.tcp_connect(host, port, tm)
        if connection == nil then
            return error_sm(E_NO_CONNECTION, errno.strerror(errno()))
        end
        local size = IPROTO_GREETING_SIZE
        local err, msg = send_and_recv(size, tm - (fiber.time() - tm_begin))
        if err then
            return error_sm(err, msg)
        end
        local g = decode_greeting(ffi.string(recv_buf.rpos, size))
        recv_buf.rpos = recv_buf.rpos + size
        if not g then
            return error_sm(E_NO_CONNECTION, 'Can\'t decode handshake')
        end
        err, msg = callback('handshake', g)
        if err then
            return error_sm(err, msg)
        end
        if g.protocol == 'Lua console' then
            local setup_delimiter = 'require("console").delimiter("$EOF$")\n'
            method_codec.inject(send_buf, nil, nil, setup_delimiter)
            local err, response = send_and_recv_console()
            if err then
                return error_sm(err, response)
            elseif response ~= '---\n...\n' then
                return error_sm(E_NO_CONNECTION, 'Unexpected response')
            end
            local rid = next_request_id
            set_state('active')
            return console_sm(rid)
        elseif g.protocol == 'Binary' then
            return iproto_auth_sm(g.salt)
        else
            return error_sm(E_NO_CONNECTION, 'Unknown protocol: ' .. g.protocol)
        end
    end

    console_sm = function(rid)
        local delim = '\n...\n'
        local err, response = send_and_recv_console()
        if err then
            return error_sm(err, response)
        else
            dispatch_response(rid, nil, response)
            return console_sm(next_id(rid))
        end
    end

    iproto_auth_sm = function(salt)
        set_state('auth')
        if not user or not password then
            set_state('fetch_schema')
            return iproto_schema_sm()
        end
        encode_auth(send_buf, new_request_id(), nil, user, password, salt)
        local err, hdr, body = send_and_recv_iproto()
        if err then
            return error_sm(err, hdr)
        end
        if hdr[IPROTO_STATUS_KEY] ~= 0 then
            return error_sm(E_NO_CONNECTION, body[IPROTO_ERROR_KEY])
        end
        set_state('fetch_schema')
        return iproto_schema_sm(hdr[IPROTO_SCHEMA_ID_KEY])
    end

    iproto_schema_sm = function(schema_id)
        if not callback('will_fetch_schema') then
            set_state('active')
            return iproto_sm(schema_id)
        end
        local select1_id = new_request_id()
        local select2_id = new_request_id()
        local response = {}
        -- fetch everything from space _vspace, 2 = ITER_ALL
        encode_select(send_buf, select1_id, nil, VSPACE_ID, 0, 2, 0, 0xFFFFFFFF, nil)
        -- fetch everything from space _vindex, 2 = ITER_ALL
        encode_select(send_buf, select2_id, nil, VINDEX_ID, 0, 2, 0, 0xFFFFFFFF, nil)
        schema_id = nil -- any schema_id will do provided that
                        -- it is consistent across responses
        repeat
            local err, hdr, body = send_and_recv_iproto()
            if err then return error_sm(err, hdr) end
            dispatch_response_iproto(hdr, body)
            local id = hdr[IPROTO_SYNC_KEY]
            if id == select1_id or id == select2_id then
                -- response to a schema query we've submitted
                local status = hdr[IPROTO_STATUS_KEY]
                local response_schema_id = hdr[IPROTO_SCHEMA_ID_KEY]
                if status ~= 0 then
                    return error_sm(E_NO_CONNECTION, body[IPROTO_ERROR_KEY])
                end
                if schema_id == nil then
                    schema_id = response_schema_id
                elseif schema_id ~= response_schema_id then
                    -- schema changed while fetching schema; restart loader
                    return iproto_schema_sm()
                end
                response[id] = body[IPROTO_DATA_KEY]
            end
        until response[select1_id] and response[select2_id]
        callback('did_fetch_schema', schema_id,
                 response[select1_id], response[select2_id])
        set_state('active')
        return iproto_sm(schema_id)
    end

    iproto_sm = function(schema_id)
        local err, hdr, body = send_and_recv_iproto()
        if err then return error_sm(err, hdr) end
        dispatch_response_iproto(hdr, body)
        local status = hdr[IPROTO_STATUS_KEY]
        local response_schema_id = hdr[IPROTO_SCHEMA_ID_KEY]
        if response_schema_id > 0 and response_schema_id ~= schema_id then
            -- schema_id has been changed - start to load a new version.
            -- Sic: self._schema_id will be updated only after reload.
            set_state('fetch_schema',
                      E_WRONG_SCHEMA_VERSION, body[IPROTO_ERROR_KEY],
                      response_schema_id)
            return iproto_schema_sm(schema_id)
        end
        return iproto_sm(schema_id)
    end

    error_sm = function(err, msg)
        if connection then connection:close(); connection = nil end
        send_buf:recycle()
        recv_buf:recycle()
        set_state('error_reconnect', err, msg)
        if callback('will_reconnect', err, msg) then
            set_state('connecting')
            return protocol_sm()
        else
            set_state('error', err, msg)
        end
    end

    return {
        close           = close,
        connect         = connect,
        wait_state      = wait_state,
        perform_request = perform_request
    }
end

-- Wrap create_transport, adding auto-close-on-GC feature.
-- All the GC magic is neatly encapsulated!
-- The tricky part is the callback:
--  * callback (typically) references the transport (indirectly);
--  * worker fiber references the callback;
--  * fibers are GC roots - i.e. transport is never GC-ed!
-- We solve the issue by making the worker->callback ref weak.
-- Now it is necessary to have a strong ref to callback somewhere or
-- it is GC-ed prematurely. We wrap close() method, stashing the
-- ref in an upvalue (close() performance doesn't matter much.)
local create_transport = function(host, port, user, password, callback)
    local weak_refs = setmetatable({callback = callback}, {__mode = 'v'})
    local function weak_callback(...)
        local callback = weak_refs.callback
        if callback then return callback(...) end
    end
    local transport = create_transport(host, port, user,
                                       password, weak_callback)
    local transport_close = transport.close
    local gc_hook = ffi.gc(ffi.new('char[1]'), function()
        pcall(transport_close)
    end)
    transport.close = function()
        -- dummy gc_hook, callback refs prevent premature GC
        return transport_close(gc_hook, callback)
    end
    return transport
end

local function parse_connect_params(host_or_uri, ...) -- self? host_or_uri port? opts?
    local port, opts = ...
    if type(host_or_uri) == 'table' then host_or_uri, port, opts = ... end
    if type(port) == 'table' then opts = port; port = nil end
    if opts == nil then opts = {} else
        local copy = {}
        for k, v in pairs(opts) do copy[k] = v end
        opts = copy
    end
    local host = host_or_uri
    if port == nil then
        local url = urilib.parse(tostring(host))
        if url == nil or url.service == nil then
            box.error(E_PROC_LUA,
                "usage: connect(uri[, opts] | host, port[, opts])")
        end
        host, port = url.host, url.service
        if opts.user == nil and opts.password == nil then
            opts.user, opts.password = url.login, url.password
        end
    end
    return host, port, opts
end

local function remote_serialize(self)
    return {
        host = self.host,
        port = self.port,
        opts = next(self.opts) and self.opts,
        state = self.state,
        error = self.error,
        protocol = self.protocol,
        peer_uuid = self.peer_uuid,
        peer_version_id = self.peer_version_id
    }
end

local remote_methods = {}
local remote_mt = {
    __index = remote_methods, __serialize = remote_serialize,
    __metatable = false
}

local console_methods = {}
local console_mt = {
    __index = console_methods, __serialize = remote_serialize,
    __metatable = false
}

local space_metatable, index_metatable

local function connect(...)
    local host, port, opts = parse_connect_params(...)
    local user, password = opts.user, opts.password; opts.password = nil
    local remote = {host = host, port = port, opts = opts, state = 'initial'}
    local function callback(what, ...)
        if      what == 'state_changed' then
            local state, errno, err = ...
            remote.state, remote.error = state, err
            if state == 'error_reconnect' then
                log.warn('%s:%s: %s', host or "", port or "", err)
            end
        elseif what == 'handshake' then
            local greeting = ...
            if not opts.console and greeting.protocol ~= 'Binary' then
                return E_NO_CONNECTION,
                       'Unsupported protocol: '..greeting.protocol
            end
            remote.protocol = greeting.protocol
            remote.peer_uuid = greeting.uuid
            remote.peer_version_id = greeting.version_id
        elseif what == 'will_fetch_schema' then
            return not opts.console
        elseif what == 'fetch_connect_timeout' then
            return opts.connect_timeout or 10
        elseif what == 'did_fetch_schema' then
            remote:_install_schema(...)
        elseif what == 'will_reconnect' then
            if type(opts.reconnect_after) == 'number' then
                fiber.sleep(opts.reconnect_after); return true
            end
        end
    end
    if opts.console then
        setmetatable(remote, console_mt)
    else
        setmetatable(remote, remote_mt)
        remote._deadlines = setmetatable({}, {__mode = 'k'})
        remote._space_mt = space_metatable(remote)
        remote._index_mt = index_metatable(remote)
        if opts.call_16 then remote.call = remote.call_16 end
    end
    remote._on_schema_reload = trigger.new("on_schema_reload")
    remote._transport = create_transport(host, port, user, password, callback)
    remote._transport.connect()
    if opts.wait_connected ~= false then
        remote._transport.wait_state('active', tonumber(opts.wait_connected))
    end
    return remote
end

local function remote_check(remote, method)
    if type(remote) ~= 'table' then
        local fmt = 'Use remote:%s(...) instead of remote.%s(...):'
        box.error(E_PROC_LUA, string.format(fmt, method, method))
    end
end

function remote_methods:close()
    remote_check(self, 'close')
    self._transport.close()
end

function remote_methods:on_schema_reload(...)
    remote_check(self, 'on_schema_reload')
    return self._on_schema_reload(...)
end

function remote_methods:is_connected()
    remote_check(self, 'is_connected')
    return self.state == 'active'
end

function remote_methods:wait_connected(timeout)
    remote_check(self, 'wait_connected')
    return self._transport.wait_state('active', timeout)
end

function remote_methods:_request(method, ...)
    local this_fiber = fiber_self()
    local transport = self._transport
    local perform_request = transport.perform_request
    local wait_state = transport.wait_state
    local deadlines = self._deadlines
    local deadline = deadlines[this_fiber]
    local err, res
    repeat
        local timeout = deadline and max(0, deadline - fiber_time())
        if self.state ~= 'active' then
            wait_state('active', timeout)
            timeout = deadline and max(0, deadline - fiber_time())
        end
        err, res = perform_request(timeout, method,
                                   self._schema_id, ...)
        if not err then
            setmetatable(res, sequence_mt)
            local postproc = method ~= 'eval' and method ~= 'call_17'
            if postproc and rawget(box, 'tuple') then
                local tnew = box.tuple.new
                for i, v in pairs(res) do
                    res[i] = tnew(v)
                end
            end
            return res
        elseif err == E_WRONG_SCHEMA_VERSION then
            err = nil
        end
    until err
    box.error({code = err, reason = res})
end

function remote_methods:ping()
    remote_check(self, 'ping')
    local deadline = self._deadlines[fiber_self()]
    local timeout = deadline and max(0, deadline-fiber_time())
    local err = self._transport.perform_request(timeout, 'ping',
                                                self._schema_id)
    return not err or err == E_WRONG_SCHEMA_VERSION
end

function remote_methods:reload_schema()
    remote_check(self, 'reload_schema')
    self:_request('select', VSPACE_ID, 0, nil, {limit = 0})
end

function remote_methods:call_16(func_name, ...)
    remote_check(self, 'call')
    return self:_request('call_16', tostring(func_name), {...})
end

function remote_methods:call(func_name, ...)
    remote_check(self, 'call')
    return unpack(self:_request('call_17', tostring(func_name), {...}))
end

function remote_methods:eval(code, ...)
    remote_check(self, 'eval')
    return unpack(self:_request('eval', code, {...}))
end

function remote_methods:wait_state(state, timeout)
    remote_check(self, 'wait_state')
    if timeout == nil then
        local deadline = self._deadlines[fiber_self()]
        timeout = deadline and max(0, deadline-fiber_time())
    end
    return self._transport.wait_state(state, timeout)
end

function remote_methods:timeout(timeout)
    remote_check(self, 'timeout')
    self._deadlines[fiber_self()] = (timeout and fiber_time() + timeout)
    return self
end

function remote_methods:_install_schema(schema_id, spaces, indices)
    local sl, space_mt, index_mt = {}, self._space_mt, self._index_mt
    for _, space in pairs(spaces) do
        local name = space[3]
        local id = space[1]
        local engine = space[4]
        local field_count = space[5]

        local s = {
            id              = id,
            name            = name,
            engine          = engine,
            field_count     = field_count,
            enabled         = true,
            index           = {},
            temporary       = false
        }
        if #space > 5 then
            local opts = space[6]
            if type(opts) == 'table' then
                -- Tarantool >= 1.7.0
                s.temporary = not not opts.temporary
            elseif type(opts) == 'string' then
                -- Tarantool < 1.7.0
                s.temporary = string.match(opts, 'temporary') ~= nil
            end
        end

        setmetatable(s, space_mt)

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
                local pktype = index[PARTS][k][2]
                local pkfield = index[PARTS][k][1]

                local pk = {
                    type = pktype,
                    fieldno = pkfield + 1
                }
                idx.parts[k] = pk
            end
            idx.unique = not not index[OPTS].is_unique
        end

        if sl[idx.space] ~= nil then
            sl[idx.space].index[idx.id] = idx
            sl[idx.space].index[idx.name] = idx
            idx.space = sl[idx.space]
            setmetatable(idx, index_mt)
        end
    end

    self._schema_id = schema_id
    self.space = sl
    self._on_schema_reload:run(self)
end

-- console methods
console_methods.close = remote_methods.close
console_methods.on_schema_reload = remote_methods.on_schema_reload
console_methods.is_connected = remote_methods.is_connected
console_methods.wait_state = remote_methods.wait_state
function console_methods:eval(line, timeout)
    remote_check(self, 'eval')
    local err, res
    local transport = self._transport
    local pr = transport.perform_request
    if self.state ~= 'active' then
        local deadline = fiber_time() + (timeout or TIMEOUT_INFINITY)
        transport.wait_state('active', timeout)
        timeout = max(0, deadline - fiber_time())
    end
    if self.protocol == 'Binary' then
        local loader = 'return require("console").eval(...)'
        err, res = pr(timeout, 'eval', nil, loader, {line})
    else
        assert(self.protocol == 'Lua console')
        err, res = pr(timeout, 'inject', nil, line..'$EOF$\n')
    end
    if err then
        box.error({code = err, reason = res})
    end
    return res[1] or res
end

-- space, index metatable
local function space_check(space, method)
    if type(space) ~= 'table' or space.id == nil then
        local fmt = 'Use space:%s(...) instead of space.%s(...)'
        box.error(E_PROC_LUA, string.format(fmt, method, method))
    end
end

local function one_tuple(tab)
    if tab[1] ~= nil then return tab[1] end
end

space_metatable = function(remote)
    local methods = {}

    function methods:insert(tuple)
        space_check(self, 'insert')
        return one_tuple(remote:_request('insert', self.id, tuple))
    end

    function methods:replace(tuple)
        space_check(self, 'replace')
        return one_tuple(remote:_request('replace', self.id, tuple))
    end

    function methods:select(key, opts)
        space_check(self, 'select')
        return remote:_request('select', self.id, 0, key, opts)
    end

    function methods:delete(key)
        space_check(self, 'delete')
        return one_tuple(remote:_request('delete', self.id, 0, key))
    end

    function methods:update(key, oplist)
        space_check(self, 'update')
        return one_tuple(remote:_request('update', self.id, 0, key, oplist))
    end

    function methods:upsert(key, oplist)
        space_check(self, 'upsert')
        return one_tuple(remote:_request('upsert', self.id, key, oplist))
    end

    function methods:get(key)
        space_check(self, 'get')
        local res = remote:_request('select', self.id, 0, key,
                                    { limit = 2, iterator = 'EQ' })
        if res[2] ~= nil then box.error(box.error.MORE_THAN_ONE_TUPLE) end
        if res[1] ~= nil then return res[1] end
    end

    return { __index = methods, __metatable = false }
end

local function index_check(index, method)
    if type(index) ~= 'table' or index.id == nil then
        local fmt = 'Use index:%s(...) instead of index.%s(...)'
        box.error(E_PROC_LUA, string.format(fmt, method, method))
    end
end

index_metatable = function(remote)
    local methods = {}

    function methods:select(key, opts)
        index_check(self, 'select')
        return remote:_request('select', self.space.id, self.id, key, opts)
    end

    function methods:get(key)
        index_check(self, 'get')
        local res = remote:_request('select', self.space.id, self.id, key,
                                    { limit = 2, iterator = 'EQ' })
        if res[2] ~= nil then box.error(box.error.MORE_THAN_ONE_TUPLE) end
        if res[1] ~= nil then return res[1] end
    end

    function methods:min(key)
        index_check(self, 'min')
        local res = remote:_request('select', self.space.id, self.id, key,
                                    { limit = 1, iterator = 'GE' })
        return one_tuple(res)
    end

    function methods:max(key)
        index_check(self, 'max')
        local res = remote:_request('select', self.space.id, self.id, key,
                                    { limit = 1, iterator = 'LE' })
        return one_tuple(res)
    end

    function methods:count(key)
        index_check(self, 'count')
        local code = string.format('box.space.%s.index.%s:count',
                                   self.space.name, self.name)
        return remote:_request('call_16', code, { key })[1][1]
    end

    function methods:delete(key)
        index_check(self, 'delete')
        local res = remote:_request('delete', self.space.id, self.id, key)
        return one_tuple(res)
    end

    function methods:update(key, oplist)
        index_check(self, 'update')
        local res = remote:_request('update', self.space.id, self.id,
                                    key, oplist)
        return one_tuple(res)
    end

    return { __index = methods, __metatable = false }
end

local this_module = {
    create_transport = create_transport,
    connect = connect,
    new = connect -- Tarantool < 1.7.1 compatibility
}

function this_module.timeout(timeout, ...)
    if type(timeout) == 'table' then timeout = ... end
    if not timeout then return this_module end
    local function timed_connect(...)
        local host, port, opts = parse_connect_params(...)
        if opts.wait_connected ~= false then opts.wait_connected = timeout end
        return connect(host, port, opts)
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
    return ...
end

this_module.self = {
    ping = function() return true end,
    reload_schema = function() end,
    close = function() end,
    timeout = function(self) return self end,
    wait_connected = function(self) return true end,
    is_connected = function(self) return true end,
    call = function(_box, proc_name, ...)
        if type(_box) ~= 'table' then
            box.error(box.error.PROC_LUA, "usage: remote:call(proc_name, ...)")
        end
        proc_name = tostring(proc_name)
        local status, proc, obj = pcall(package.loaded['box.internal'].
            call_loadproc, proc_name)
        if not status then
            rollback()
            return box.error() -- re-throw
        end
        local result
        if obj ~= nil then
            return handle_eval_result(pcall(proc, obj, ...))
        else
            return handle_eval_result(pcall(proc, ...))
        end
    end,
    eval = function(_box, expr, ...)
        if type(_box) ~= 'table' then
            box.error(box.error.PROC_LUA, "usage: remote:eval(expr, ...)")
        end
        local proc, errmsg = loadstring(expr)
        if not proc then
            proc, errmsg = loadstring("return "..expr)
        end
        if not proc then
            rollback()
            return box.error(box.error.PROC_LUA, errmsg)
        end
        return handle_eval_result(pcall(proc, ...))
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
