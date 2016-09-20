-- net_box.lua (internal file)
local ffi      = require('ffi')
local buffer   = require('buffer')
local socket   = require('socket')
local fiber    = require('fiber')
local msgpack  = require('msgpack')
local errno    = require('errno')
local internal = require('net.box.lib')

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
-- Events / queries delivered via a callback:
--
--  'state_changed', state, errno, error
--  'handshake', greeting           -> nil (accept) / errno, error (reject)
--  'will_fetch_schema'             -> true (approve) / false (skip fetch)
--  'did_fetch_schema', schema_id, spaces, indices
--  'will_reconnect', errno, error  -> true (approve) / false (reject)
--
-- Suggestion: sleep a few secs before approving reconnect.
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
    local requests_next_id = 1

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
            -- cancel all request but the ones bearing the particular
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
            if connection then connection:close(); connection = nil end
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
        if worker_fiber then worker_fiber:cancel(); worker_fiber = nil end
    end

    -- REQUEST/RESPONSE --
    local function perform_request(timeout, method, schema_id, ...)
        if state ~= 'active' then
            return last_errno or E_NO_CONNECTION, last_error
        end
        local deadline = fiber_time() + (timeout or TIMEOUT_INFINITY)
        -- alert worker to notify it of the queued outgoing data;
        -- if the buffer wasn't empty, assume the worker was already alerted
        if send_buf:size() == 0 then worker_fiber:wakeup() end
        local id = requests_next_id
        method_codec[method](send_buf, id, schema_id, ...)
        requests_next_id = next_id(id)
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
        local id = requests_next_id; requests_next_id = next_id(id)
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

    -- PROTOCOL STATE MACHINE (WORKER FIBER) --
    --
    -- The sm is implemented as a collection of functions performing
    -- tail-recursive calls to each other. Yep, Lua optimizes
    -- such calls, and yep, this is the canonical way to implement
    -- a state machine in Lua.
    local console_sm, iproto_auth_sm, iproto_schema_sm, iproto_sm, error_sm

    protocol_sm = function ()
        connection = socket.tcp_connect(host, port)
        if connection == nil then
            return error_sm(E_NO_CONNECTION, errno.strerror(errno()))
        end
        local size = IPROTO_GREETING_SIZE
        local err, msg = send_and_recv(size, 0.3)
        if err then return error_sm(err, msg) end
        local g = decode_greeting(ffi.string(recv_buf.rpos, size))
        recv_buf.rpos = recv_buf.rpos + size
        if not g then
            return error_sm(E_NO_CONNECTION, 'Can\'t decode handshake')
        end
        err, msg = callback('handshake', g)
        if err then return error_sm(err, msg) end
        if g.protocol == 'Lua console' then
            local rid = requests_next_id
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
        local err, delim_pos = send_and_recv(delim)
        if err then
            return error_sm(err, delim_pos)
        else
            local response = ffi.string(recv_buf.rpos, delim_pos + #delim)
            dispatch_response(rid, nil, response)
            recv_buf.rpos = recv_buf.rpos + delim_pos + #delim
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
        encode_select(send_buf, select1_id, nil, VSPACE_ID, 0, 2, 0, 0xFFFFFFFF)
        encode_select(send_buf, select2_id, nil, VINDEX_ID, 0, 2, 0, 0xFFFFFFFF)
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
        if status ~= 0 and
           band(status, IPROTO_ERRNO_MASK) == E_WRONG_SCHEMA_VERSION and
           response_schema_id ~= schema_id
        then
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
    local gc_hook = ffi.gc(ffi.new('char[1]'), transport_close)
    transport.close = function()
        -- dummy gc_hook, callback refs prevent premature GC
        return transport_close(gc_hook, callback)
    end
    return transport
end

local internal = require 'net.box.lib'
local msgpack = require 'msgpack'
local fiber = require 'fiber'
local socket = require 'socket'
local log = require 'log'
local errno = require 'errno'
local ffi = require 'ffi'
local yaml = require 'yaml'
local urilib = require 'uri'
local buffer = require 'buffer'

-- packet codes
local OK                = 0
local SELECT            = 1
local INSERT            = 2
local REPLACE           = 3
local UPDATE            = 4
local DELETE            = 5
local CALL_16           = 6
local AUTH              = 7
local EVAL              = 8
local UPSERT            = 9
local CALL              = 10
local PING              = 64
local ERROR_TYPE        = 65536

-- packet keys
local TYPE              = 0x00
local SYNC              = 0x01
local SCHEMA_ID         = 0x05
local SPACE_ID          = 0x10
local INDEX_ID          = 0x11
local LIMIT             = 0x12
local OFFSET            = 0x13
local ITERATOR          = 0x14
local INDEX_BASE        = 0x15
local KEY               = 0x20
local TUPLE             = 0x21
local FUNCTION_NAME     = 0x22
local USER              = 0x23
local EXPR              = 0x27
local OPS               = 0x28
local DATA              = 0x30
local ERROR             = 0x31
local GREETING_SIZE     = 128

local TIMEOUT_INFINITY  = 500 * 365 * 86400

local sequence_mt = { __serialize = 'sequence' }
local mapping_mt = { __serialize = 'mapping' }

local CONSOLE_FAKESYNC  = 15121974
local CONSOLE_DELIMITER = "$EOF$"

local VSPACE_ID = 281
local VINDEX_ID = 289

local ch_buf = {}
local ch_buf_size = 0

local function get_channel()
    if ch_buf_size == 0 then
        return fiber.channel()
    end

    local ch = ch_buf[ch_buf_size]
    ch_buf[ch_buf_size] = nil
    ch_buf_size = ch_buf_size - 1
    return ch
end

local function free_channel(ch)
    -- return channel to buffer
    ch_buf[ch_buf_size + 1] = ch
    ch_buf_size = ch_buf_size + 1
end

local function version_id(major, minor, patch)
    return bit.bor(bit.lshift(bit.bor(bit.lshift(major, 8), minor), 8), patch)
end

local function one_tuple(tbl)
    if tbl == nil then
        return
    end
    if #tbl > 0 then
        if rawget(box, 'tuple') ~= nil then
            return tbl[1]
        else
            return box.tuple.new(tbl[1])
        end
    end
    return
end

local function multiple_tuples(data)
    if rawget(box, 'tuple') ~= nil then
        for i, v in pairs(data) do
            data[i] = box.tuple.new(data[i])
        end
    end
    -- disable YAML flow output (useful for admin console)
    return setmetatable(data, sequence_mt)
end

local requests = {
    [PING]    = internal.encode_ping;
    [AUTH]    = internal.encode_auth;
    [CALL_16] = internal.encode_call_16;
    [CALL]    = internal.encode_call;
    [EVAL]    = internal.encode_eval;
    [INSERT]  = internal.encode_insert;
    [REPLACE] = internal.encode_replace;
    [DELETE] = internal.encode_delete;
    [UPDATE]  = internal.encode_update;
    [UPSERT]  = internal.encode_upsert;
    [SELECT]  = function(wbuf, sync, schema_id, spaceno, indexno, key, opts)
        if opts == nil then
            opts = {}
        end
        if spaceno == nil or type(spaceno) ~= 'number' then
            box.error(box.error.NO_SUCH_SPACE, '#'..tostring(spaceno))
        end

        if indexno == nil or type(indexno) ~= 'number' then
            box.error(box.error.NO_SUCH_INDEX, indexno, '#'..tostring(spaceno))
        end

        local limit, offset
        if opts.limit ~= nil then
            limit = tonumber(opts.limit)
        else
            limit = 0xFFFFFFFF
        end
        if opts.offset ~= nil then
            offset = tonumber(opts.offset)
        else
            offset = 0
        end
        local iterator = require('box.internal').check_iterator_type(opts,
            key == nil or (type(key) == 'table' and #key == 0))

        internal.encode_select(wbuf, sync, schema_id, spaceno, indexno,
            iterator, offset, limit, key)
    end;
}

local function check_if_space(space)
    if type(space) == 'table' and space.id ~= nil then
        return
    end
    error("Use space:method(...) instead space.method(...)")
end

local function space_metatable(self)
    return {
        __index = {
            insert  = function(space, tuple)
                check_if_space(space)
                return self:_insert(space.id, tuple)
            end,

            replace = function(space, tuple)
                check_if_space(space)
                return self:_replace(space.id, tuple)
            end,

            select = function(space, key, opts)
                check_if_space(space)
                return self:_select(space.id, 0, key, opts)
            end,

            delete = function(space, key)
                check_if_space(space)
                return self:_delete(space.id, key, 0)
            end,

            update = function(space, key, oplist)
                check_if_space(space)
                return self:_update(space.id, key, oplist, 0)
            end,

            upsert = function(space, tuple_key, oplist)
                check_if_space(space)
                return self:_upsert(space.id, tuple_key, oplist, 0)
            end,

            get = function(space, key)
                check_if_space(space)
                local res = self:_select(space.id, 0, key,
                                    { limit = 2, iterator = 'EQ' })
                if #res == 0 then
                    return
                end
                if #res == 1 then
                    return res[1]
                end
                box.error(box.error.MORE_THAN_ONE_TUPLE)
            end
        }
    }
end

local function check_if_index(idx)
    if type(idx) == 'table' and idx.id ~= nil and type(idx.space) == 'table' then
        return
    end
    error('Use index:method(...) instead index.method(...)')
end

local function index_metatable(self)
    return {
        __index = {
            select = function(idx, key, opts)
                check_if_index(idx)
                return self:_select(idx.space.id, idx.id, key, opts)
            end,

            get = function(idx, key)
                check_if_index(idx)
                local res = self:_select(idx.space.id, idx.id, key,
                                    { limit = 2, iterator = 'EQ' })
                if #res == 0 then
                    return
                end
                if #res == 1 then
                    return res[1]
                end
                box.error(box.error.MORE_THAN_ONE_TUPLE)
            end,

            min = function(idx, key)
                check_if_index(idx)
                local res = self:_select(idx.space.id, idx.id, key,
                    { limit = 1, iterator = 'GE' })
                if #res > 0 then
                    return res[1]
                end
            end,

            max = function(idx, key)
                check_if_index(idx)
                local res = self:_select(idx.space.id, idx.id, key,
                    { limit = 1, iterator = 'LE' })
                if #res > 0 then
                    return res[1]
                end
            end,

            count = function(idx, key)
                check_if_index(idx)
                local proc = string.format('box.space.%s.index.%s:count',
                    idx.space.name, idx.name)
                local res = self:call_16(proc, key)
                if #res > 0 then
                    return res[1][1]
                end
            end,

            delete = function(idx, key)
                check_if_index(idx)
                return self:_delete(idx.space.id, key, idx.id)
            end,

            update = function(idx, key, oplist)
                check_if_index(idx)
                return self:_update(idx.space.id, key, oplist, idx.id)
            end,

            upsert = function(idx, tuple_key, oplist)
                check_if_index(idx)
                return self:_upsert(idx.space.id, tuple_key, oplist, idx.id)
            end,

        }
    }
end

local errno_is_transient = {
    [errno.EAGAIN] = true;
    [errno.EWOULDBLOCK] = true;
    [errno.EINTR] = true;
}

local remote = {}

local remote_methods = {
    connect = function(cls, host, port, opts)
        local self = { _sync = -1 }

        if type(cls) == 'table' then
            setmetatable(self, getmetatable(cls))
        else
            host, port, opts = cls, host, port
            setmetatable(self, getmetatable(remote))
        end


        -- uri as the first argument
        if opts == nil then
            opts = {}
            if type(port) == 'table' then
                opts = port
                port = nil
            end

            if port == nil then

                local address = urilib.parse(tostring(host))
                if address == nil or address.service == nil then
                    box.error(box.error.PROC_LUA,
                        "usage: remote:new(uri[, opts] | host, port[, opts])")
                end

                host = address.host
                port = address.service

                opts.user = address.login or opts.user
                opts.password = address.password or opts.password
            end
        end


        self.is_instance = true
        self.host = host
        self.port = port
        self.opts = opts

        if self.opts == nil then
            self.opts = {}
        end

        if self.opts.user ~= nil and self.opts.password == nil then
            self.opts.password = ""
        end
        if self.opts.user == nil and self.opts.password ~= nil then
            box.error(box.error.PROC_LUA,
                "net.box: user is not defined")
        end


        if self.host == nil then
            self.host = 'localhost'
        end

        self.is_run = true
        self.state = 'init'
        self.wbuf = buffer.ibuf(buffer.READAHEAD)
        self.rpos = 1
        self.rlen = 0
        self._schema_id = 0

        self.ch = { sync = {}, fid = {} }
        self.wait = { state = {} }
        self.timeouts = {}

        fiber.create(function() self:_connect_worker() end)
        fiber.create(function() self:_read_worker() end)
        fiber.create(function() self:_write_worker() end)

        if self.opts.wait_connected == nil or self.opts.wait_connected then
            self:wait_connected()
        end

        return self
    end,

    -- sync
    sync    = function(self)
        self._sync = self._sync + 1
        if self._sync >= 0x7FFFFFFF then
            self._sync = 0
        end
        return self._sync
    end,

    ping    = function(self)
        if type(self) ~= 'table' then
            box.error(box.error.PROC_LUA, "usage: remote:ping()")
        end
        if not self:is_connected() then
            return false
        end
        local res = self:_request(PING, false)


        if res == nil then
            return false
        end

        if res.hdr[TYPE] == OK then
            return true
        end
        return false
    end,

    _console = function(self, line)
        local data = line..CONSOLE_DELIMITER.."\n\n"
        ffi.copy(self.wbuf.wpos, data, #data)
        self.wbuf.wpos = self.wbuf.wpos + #data

        local res = self:_request_raw(EVAL, CONSOLE_FAKESYNC, data, true)
        return res.body[DATA]
    end,

    call_16  = function(self, proc_name, ...)
        if type(self) ~= 'table' then
            box.error(box.error.PROC_LUA, "usage: remote:call_16(proc_name, ...)")
        end

        proc_name = tostring(proc_name)

        local res = self:_request(CALL_16, true, proc_name, {...})
        -- disable YAML flow output (useful for admin console)
        return setmetatable(res.body[DATA], sequence_mt)
    end,

    call_17  = function(self, proc_name, ...)
        if type(self) ~= 'table' then
            box.error(box.error.PROC_LUA, "usage: remote:call(proc_name, ...)")
        end

        proc_name = tostring(proc_name)

        local data = self:_request(CALL, true, proc_name, {...}).body[DATA]
        local data_len = #data
        if data_len == 1 then
            return data[1]
        elseif data_len == 0 then
            return
        else
            return unpack(data)
        end
    end,

    eval    = function(self, expr, ...)
        if type(self) ~= 'table' then
            box.error(box.error.PROC_LUA, "usage: remote:eval(expr, ...)")
        end

        expr = tostring(expr)
        local data = self:_request(EVAL, true, expr, {...}).body[DATA]
        local data_len = #data
        if data_len == 1 then
            return data[1]
        elseif data_len == 0 then
            return
        else
            return unpack(data)
        end
    end,

    is_connected = function(self)
        return self.state == 'active' or self.state == 'activew'
    end,

    wait_connected = function(self, timeout)
        return self:_wait_state(self._request_states, timeout)
    end,

    timeout = function(self, timeout)
        if timeout == nil then
            return self
        end
        if self.is_instance then
            self.timeouts[ fiber.id() ] = timeout
            return self
        end

        local connect = function(cls, host, port, opts)
            if opts == nil then
                opts = {}
            end

            opts.wait_connected = false

            local cn = self:new(host, port, opts)

            if not cn:wait_connected(timeout) then
                cn:close()
                box.error(box.error.TIMEOUT)
            end
            return cn
        end
        return { connect = connect, new = connect }
    end,


    close = function(self)
        if self.state ~= 'closed' then
            self:_switch_state('closed')
            self:_error_waiters('Connection was closed')
            if self.s ~= nil then
                self.s:close()
                self.s = nil
            end
        end
    end,

    -- private methods
    _fatal = function(self, efmt, ...)
        if self.state == 'error' then
            return
        end
        local emsg = efmt
        if select('#', ...) > 0 then
            emsg = string.format(efmt, ...)
        end

        if self.s ~= nil then
            self.s:close()
            self.s = nil
        end

        log.warn("%s:%s: %s", self.host or "", self.port or "", tostring(emsg))
        self.error = emsg
        self.space = {}
        self:_switch_state('error')
        self:_error_waiters(emsg)
        self.rpos = 1
        self.rlen = 0
        self.version_id = nil
        self.version = nil
        self.salt = nil
    end,

    _wakeup_client = function(self, hdr, body)
        local sync = hdr[SYNC]

        local ch = self.ch.sync[sync]
        if ch ~= nil then
            ch.response = { hdr = hdr, body = body }
            fiber.wakeup(ch.fid)
        else
            log.warn("Unexpected response %s", tostring(sync))
        end
    end,

    _error_waiters = function(self, emsg)
        local waiters = self.ch.sync
        self.ch.sync = {}
        for sync, channel in pairs(waiters) do
            channel.response = {
                hdr = {
                    [TYPE] = bit.bor(ERROR_TYPE, box.error.NO_CONNECTION),
                    [SYNC] = sync
                },
                body = {
                    [ERROR] = emsg
                }
            }
            fiber.wakeup(channel.fid)
        end
    end,

    _check_console_response = function(self)
        local docend = "\n...\n"
        local resp = ffi.string(self.rbuf.rpos, self.rbuf:size())

        if #resp < #docend or
                string.sub(resp, #resp + 1 - #docend) ~= docend then
            return -1
        end

        local hdr = { [SYNC] = CONSOLE_FAKESYNC, [TYPE] = 0 }
        local body = { [DATA] = resp }

        self:_wakeup_client(hdr, body)
        self.rbuf:read(#resp)
        return 0
    end,

    _check_binary_response = function(self)
        while true do
            if self.rbuf.rpos + 5 > self.rbuf.wpos then
                return 5 - (self.rbuf.wpos - self.rbuf.rpos)
            end

            local rpos, len = msgpack.ibuf_decode(self.rbuf.rpos)
            if rpos + len > self.rbuf.wpos then
                return len - (self.rbuf.wpos - rpos)
            end

            local rpos, hdr = msgpack.ibuf_decode(rpos)
            local body = {}

            if rpos < self.rbuf.wpos then
                rpos, body= msgpack.ibuf_decode(rpos)
            end
            setmetatable(body, mapping_mt)

            self.rbuf.rpos = rpos
            self:_wakeup_client(hdr, body)

           if self.rbuf:size() == 0 then
                return 0
            end
        end
    end,

    _switch_state = function(self, state)
        if self.state == state or state == nil then
            return
        end
        self.state = state

        local list = self.wait.state[ state ] or {}
        self.wait.state[ state ] = {}

        for _, fid in pairs(list) do
            if fid ~= fiber.id() then
                if self.ch.fid[fid] ~= nil then
                    self.ch.fid[fid]:put(true)
                    self.ch.fid[fid] = nil
                end
            end
        end
    end,

    _wait_state = function(self, states, timeout)
        timeout = timeout or TIMEOUT_INFINITY
        while timeout > 0 and self:_is_state(states) ~= true do
            local started = fiber.time()
            local fid = fiber.id()
            local ch = get_channel()
            for state, _ in pairs(states) do
                self.wait.state[state] = self.wait.state[state] or {}
                self.wait.state[state][fid] = fid
            end

            self.ch.fid[fid] = ch
            ch:get(timeout)
            self.ch.fid[fid] = nil
            free_channel(ch)

            for state, _ in pairs(states) do
                self.wait.state[state][fid] = nil
            end
            timeout = timeout - (fiber.time() - started)
        end
        return self.state
    end,

    _connect_worker = function(self)
        fiber.name('net.box.connector')
        local connect_states = { init = true, error = true, closed = true }
        while self:_wait_state(connect_states) ~= 'closed' do

            if self.state == 'error' then
                if self.opts.reconnect_after == nil then
                    self:_switch_state('closed')
                    return
                end
                fiber.sleep(self.opts.reconnect_after)
            end

            self:_switch_state('connecting')

            self.s = socket.tcp_connect(self.host, self.port)
            if self.s == nil then
                self:_fatal(errno.strerror())
            else

                -- on_connect
                self:_switch_state('handshake')
                local greetingbuf = self.s:read(GREETING_SIZE)
                if greetingbuf == nil then
                    self:_fatal(errno.strerror())
                elseif #greetingbuf ~= GREETING_SIZE then
                    self:_fatal("Can't read handshake")
                else
                    local greeting = internal.decode_greeting(greetingbuf)
                    if not greeting then
                        self:_fatal("Can't decode handshake")
                    elseif greeting.protocol == 'Lua console' then
                        self.version_id = greeting.version_id
                        -- enable self:console() method
                        self.console = self._console
                        self._check_response = self._check_console_response
                        -- set delimiter
                        self:_switch_state('schema')

                        local line = "require('console').delimiter('"..CONSOLE_DELIMITER.."')\n\n"
                        ffi.copy(self.wbuf.wpos, line, #line)
                        self.wbuf.wpos = self.wbuf.wpos + #line

                        local res = self:_request_raw(EVAL, CONSOLE_FAKESYNC, line, true)

                        if res.hdr[TYPE] ~= OK then
                            self:_fatal(res.body[ERROR])
                        end
                        self:_switch_state('active')
                    elseif greeting.protocol == 'Binary' then
                        self.version_id = greeting.version_id
                        self.salt = greeting.salt
                        self.console = nil
                        self._check_response = self._check_binary_response
                        if self.version_id < version_id(1, 7, 1) then
                            -- Tarantool < 1.7.1 compatibility
                            self.call = self.call_16
                        else
                            self.call = self.call_17
                        end
                        local s, e = pcall(function()
                            self:_auth()
                        end)
                        if not s then
                            self:_fatal(e)
                        end

                        self:reload_schema()
                    else
                        self:_fatal("Unsupported protocol - "..
                            greeting.protocol)
                    end
                end
            end
        end
    end,

    _auth = function(self)
        if self.opts.user == nil or self.opts.password == nil then
            return
        end

        self:_switch_state('auth')

        local auth_res = self:_request_internal(AUTH,
            false, self.opts.user, self.opts.password, self.salt)

        if auth_res.hdr[TYPE] ~= OK then
            self:_fatal(auth_res.body[ERROR])
        end
    end,

    -- states wakeup _read_worker
    _r_states = {
        active = true, activew = true, schema = true,
        schemaw = true, auth = true, authw = true,
        closed = true,
    },
    -- states wakeup _write_worker
    _rw_states = {
        activew = true, schemaw = true, authw = true,
        closed = true,
    },
    _request_states = {
        active = true, activew = true, closed = true,
    },

    _is_state = function(self, states)
        return states[self.state] ~= nil
    end,

    reload_schema = function(self)
        self._schema_id = 0
        if self.state == 'closed' or self.state == 'error' then
            self:_fatal('Can not load schema from the state')
            return
        end

        self:_switch_state('schema')

        local resp = self:_request_internal(SELECT,
            true, VSPACE_ID, 0, nil, { iterator = 'ALL' })

        -- set new schema id after first request
        self._schema_id = resp.hdr[SCHEMA_ID]

        local spaces = resp.body[DATA]
        resp = self:_request_internal(SELECT,
            true, VINDEX_ID, 0, nil, { iterator = 'ALL' })
        local indexes = resp.body[DATA]

        local sl = {}

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

            setmetatable(s, space_metatable(self))

            sl[id] = s
            sl[name] = s
        end

        for _, index in pairs(indexes) do
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
                idx.unique = index[OPTS] == 1 and true or false

                for k = 0, index[PARTS] - 1 do
                    local pktype = index[7 + k * 2 + 1]
                    local pkfield = index[7 + k * 2]

                    local pk = {
                        type = string.upper(pktype),
                        fieldno = pkfield
                    }
                    idx.parts[k] = pk
                end
            else
                for k = 1, #index[PARTS] do
                    local pktype = index[PARTS][k][2]
                    local pkfield = index[PARTS][k][1]

                    local pk = {
                        type = string.upper(pktype),
                        fieldno = pkfield
                    }
                    idx.parts[k - 1] = pk
                end
                idx.unique = index[OPTS].is_unique and true or false
            end

            if sl[idx.space] ~= nil then
                sl[idx.space].index[idx.id] = idx
                sl[idx.space].index[idx.name] = idx
                idx.space = sl[idx.space]
                setmetatable(idx, index_metatable(self))
            end
        end

        self.space = sl
        if self.state ~= 'error' and self.state ~= 'closed' then
            self:_switch_state('active')
        end
    end,

    _read_worker = function(self)
        fiber.name('net.box.read')
        self.rbuf = buffer.ibuf(buffer.READAHEAD)
        while self:_wait_state(self._r_states) ~= 'closed' do
            if self.s:readable() then
                local len = self.s:sysread(self.rbuf.wpos, self.rbuf:unused())
                if len ~= nil then
                    if len == 0 then
                        self:_fatal('Remote host closed connection')
                    else
                        self.rbuf.wpos = self.rbuf.wpos + len

                        local advance = self:_check_response()
                        if advance <= 0 then
                            advance = buffer.READAHEAD
                        end

                        self.rbuf:reserve(advance)
                    end
                elseif errno_is_transient[errno()] ~= true then
                    self:_fatal(errno.strerror())
                end
            end
        end
    end,

    _to_wstate = { active = 'activew', schema = 'schemaw', auth = 'authw' },
    _to_rstate = { activew = 'active', schemaw = 'schema', authw = 'auth' },

    _write_worker = function(self)
        fiber.name('net.box.write')
        while self:_wait_state(self._rw_states) ~= 'closed' do
            while self.wbuf:size() > 0 do
                local written = self.s:syswrite(self.wbuf.rpos, self.wbuf:size())
                if written ~= nil then
                    self.wbuf.rpos = self.wbuf.rpos + written
                else
                    if errno_is_transient[errno()] then
                    -- the write is with a timeout to detect FIN
                    -- packet on the receiving end, and close the connection.
                    -- Every second sockets we iterate the while loop
                    -- and check the connection state
                        while self.s:writable(1) == 0 and self.state ~= 'closed' do
                        end
                    else
                        self:_fatal(errno.strerror())
                        break
                    end
                end
            end
            self.wbuf:reserve(buffer.READAHEAD)
            self:_switch_state(self._to_rstate[self.state])
        end
    end,

    _request = function(self, reqtype, raise, ...)
        if self.console then
            box.error(box.error.UNSUPPORTED, "console", "this request type")
        end
        local fid = fiber.id()
        if self.timeouts[fid] == nil then
            self.timeouts[fid] = TIMEOUT_INFINITY
        end

        local started = fiber.time()

        self:_wait_state(self._request_states, self.timeouts[fid])

        self.timeouts[fid] = self.timeouts[fid] - (fiber.time() - started)

        if self.state == 'closed' then
            if raise then
                box.error(box.error.NO_CONNECTION)
            end
        end

        if self.timeouts[fid] <= 0 then
            self.timeouts[fid] = nil
            if raise then
                box.error(box.error.TIMEOUT)
            else
                return {
                    hdr = { [TYPE] = bit.bor(ERROR_TYPE, box.error.TIMEOUT) },
                    body = { [ERROR] = 'Timeout exceeded' }
                }
            end
        end

        return self:_request_internal(reqtype, raise, ...)
    end,

    _request_raw = function(self, reqtype, sync, request, raise)

        local fid = fiber.id()
        if self.timeouts[fid] == nil then
            self.timeouts[fid] = TIMEOUT_INFINITY
        end

        self:_switch_state(self._to_wstate[self.state])

        local ch = { fid = fid; }
        self.ch.sync[sync] = ch
        fiber.sleep(self.timeouts[fid])
        local response = ch.response
        self.ch.sync[sync] = nil
        self.timeouts[fid] = nil

        if response == nil then
            if raise then
                box.error(box.error.TIMEOUT)
            else
                return {
                    hdr = { [TYPE] = bit.bor(ERROR_TYPE, box.error.TIMEOUT) },
                    body = { [ERROR] = 'Timeout exceeded' }
                }
            end
        end

        return response
    end,

    _request_internal = function(self, reqtype, raise, ...)
        while true do
            local sync = self:sync()
            local request = requests[reqtype](self.wbuf, sync, self._schema_id, ...)
            local response = self:_request_raw(reqtype, sync, request, raise)
            local resptype = response.hdr[TYPE]
            if resptype == OK then
                return response
            end
            local err_code = bit.band(resptype, bit.lshift(1, 15) - 1)
            if err_code ~= box.error.WRONG_SCHEMA_VERSION then
                if raise then
                    box.error({
                        code = err_code,
                        reason = response.body[ERROR]
                    })
                end
                return response
            end

            --
            -- Schema has been changed on the remote server. Try to reload
            -- schema and re-send request again. Please note reload_schema()
            -- also calls current function which can loop on WRONG_SCHEMA as
            -- well. This logic may lead to deep recursion if server
            -- continuously performs DDL for the long time.
            --
            self:reload_schema()
        end
        -- unreachable
    end,

    -- private (low level) methods
    _select = function(self, spaceno, indexno, key, opts)
        local res = self:_request(SELECT, true, spaceno, indexno, key, opts)
        return multiple_tuples(res.body[DATA])
    end,

    _insert = function(self, spaceno, tuple)
        local res = self:_request(INSERT, true, spaceno, tuple)
        return one_tuple(res.body[DATA])
    end,

    _replace = function(self, spaceno, tuple)
        local res = self:_request(REPLACE, true, spaceno, tuple)
        return one_tuple(res.body[DATA])
    end,

    _delete  = function(self, spaceno, key, index_id)
        local res = self:_request(DELETE, true, spaceno, index_id, key, index_id)
        return one_tuple(res.body[DATA])
    end,

    _update = function(self, spaceno, key, oplist, index_id)
        local res = self:_request(UPDATE, true, spaceno, index_id, key, oplist)
        return one_tuple(res.body[DATA])
    end,

    _upsert = function(self, spaceno, tuple_key, oplist, index_id)
        local res = self:_request(UPSERT, true, spaceno,
                                  index_id, tuple_key, oplist)
        return one_tuple(res.body[DATA])
    end,
}

-- Tarantool < 1.7.1 compatibility
remote_methods.new = remote_methods.connect
setmetatable(remote, { __index = remote_methods })

local function rollback()
    if rawget(box, 'rollback') ~= nil then
        -- roll back local transaction on error
        box.rollback()
    end
end

local function handle_eval_result(status, ...)
    if not status then
        rollback()
        return box.error(box.error.PROC_LUA, (...))
    end
    return ...
end

remote.self = {
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

setmetatable(remote.self, {
    __index = function(self, key)
        if key == 'space' then
            -- proxy self.space to box.space
            return require('box').space
        end
        return nil
    end
})

remote.create_transport = create_transport

package.loaded['net.box'] = remote
