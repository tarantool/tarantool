-- net_box.lua (internal file)
local log      = require('log')
local ffi      = require('ffi')
local buffer   = require('buffer')
local socket   = require('socket')
local fiber    = require('fiber')
local msgpack  = require('msgpack')
local urilib   = require('uri')
local internal = require('net.box.lib')
local trigger  = require('internal.trigger')

local band              = bit.band
local max               = math.max
local fiber_clock       = fiber.clock
local fiber_self        = fiber.self
local decode            = msgpack.decode_unchecked
local decode_map_header = msgpack.decode_map_header

local table_new           = require('table.new')
local check_iterator_type = box.internal.check_iterator_type
local check_index_arg     = box.internal.check_index_arg
local check_space_arg     = box.internal.check_space_arg
local check_primary_index = box.internal.check_primary_index

local communicate     = internal.communicate
local encode_auth     = internal.encode_auth
local encode_method   = internal.encode_method
local decode_greeting = internal.decode_greeting
local decode_method   = internal.decode_method
local decode_error    = internal.decode_error

local TIMEOUT_INFINITY = 500 * 365 * 86400
local VSPACE_ID        = 281
local VINDEX_ID        = 289
local VCOLLATION_ID    = 277
local DEFAULT_CONNECT_TIMEOUT = 10

local IPROTO_STATUS_KEY    = 0x00
local IPROTO_ERRNO_MASK    = 0x7FFF
local IPROTO_SYNC_KEY      = 0x01
local IPROTO_SCHEMA_VERSION_KEY = 0x05
local IPROTO_DATA_KEY      = 0x30
local IPROTO_ERROR_24      = 0x31
local IPROTO_GREETING_SIZE = 128
local IPROTO_CHUNK_KEY     = 128
local IPROTO_OK_KEY        = 0

-- select errors from box.error
local E_UNKNOWN              = box.error.UNKNOWN
local E_NO_CONNECTION        = box.error.NO_CONNECTION
local E_TIMEOUT              = box.error.TIMEOUT
local E_PROC_LUA             = box.error.PROC_LUA
local E_NO_SUCH_SPACE        = box.error.NO_SUCH_SPACE

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
-- Injects raw data into connection. Used by console and tests.
local M_INJECT      = 17

-- utility tables
local is_final_state         = {closed = 1, error = 1}

local function decode_push(raw_data)
    local response, raw_end = decode(raw_data)
    return response[IPROTO_DATA_KEY][1], raw_end
end

local function version_id(major, minor, patch)
    return bit.bor(bit.lshift(major, 16), bit.lshift(minor, 8), patch)
end

local function version_at_least(peer_version_id, major, minor, patch)
    return peer_version_id >= version_id(major, minor, patch)
end

local function next_id(id) return band(id + 1, 0x7FFFFFFF) end

--
-- Connect to a remote server, do handshake.
-- @param host Hostname.
-- @param port TCP port.
-- @param timeout Timeout to connect and receive greeting.
--
-- @retval nil, err Error occured. The reason is returned.
-- @retval two non-nils A connected socket and a decoded greeting.
--
local function establish_connection(host, port, timeout)
    local timeout = timeout or DEFAULT_CONNECT_TIMEOUT
    local begin = fiber.clock()
    local s, err = socket.tcp_connect(host, port, timeout)
    if not s then
        return nil, err
    end
    local msg = s:read({chunk = IPROTO_GREETING_SIZE},
                        timeout - (fiber.clock() - begin))
    if not msg then
        local err = s:error()
        s:close()
        return nil, err
    end
    local greeting, err = decode_greeting(msg)
    if not greeting then
        s:close()
        return nil, err
    end
    return s, greeting
end

--
-- Default action on push during a synchronous request -
-- ignore.
--
local function on_push_sync_default() end

--
-- Basically, *transport* is a TCP connection speaking one of
-- Tarantool network protocols. This is a low-level interface.
-- Primary features:
--  * implements protocols; concurrent perform_request()-s benefit from
--    multiplexing support in the protocol;
--  * schema-aware (optional) - snoops responses and initiates
--    schema reload when a response has a new schema version;
--  * delivers transport events via the callback.
--
-- Transport state machine:
--
-- State machine starts in 'initial' state. New_sm method
-- accepts an established connection and spawns a worker fiber.
-- Stop method sets the state to 'closed' and kills the worker.
-- If the transport is already in 'error' state stop() does
-- nothing.
--
-- State chart:
--
-- connecting -> initial +-> active
--                        \
--                         +-> auth -> fetch_schema <-> active
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
--  'state_changed', state, error
--  'handshake', greeting -> nil (accept) / errno, error (reject)
--  'will_fetch_schema'   -> true (approve) / false (skip fetch)
--  'did_fetch_schema', schema_version, spaces, indices
--  'reconnect_timeout'   -> get reconnect timeout if set and > 0,
--                           else nil is returned.
--
-- Suggestion for callback writers: sleep a few secs before approving
-- reconnect.
--
local function create_transport(host, port, user, password, callback,
                                connection, greeting)
    -- check / normalize credentials
    if user == nil and password ~= nil then
        box.error(E_PROC_LUA, 'net.box: user is not defined')
    end
    if user ~= nil and password == nil then password = '' end

    -- Current state machine's state.
    local state            = 'initial'
    local last_errno
    local last_error
    local state_cond       = fiber.cond() -- signaled when the state changes

    -- Async requests currently 'in flight', keyed by a request
    -- id. Value refs are weak hence if a client dies
    -- unexpectedly, GC cleans the mess.
    -- Async request can not be timed out completely. Instead a
    -- user must decide when he does not want to wait for
    -- response anymore.
    -- Sync requests are implemented as async call + immediate
    -- wait for a result.
    local requests         = setmetatable({}, { __mode = 'v' })
    local next_request_id  = 1

    local worker_fiber
    local send_buf         = buffer.ibuf(buffer.READAHEAD)
    local recv_buf         = buffer.ibuf(buffer.READAHEAD)

    --
    -- Async request metamethods.
    --
    local request_index = {}
    --
    -- When an async request is finalized (with ok or error - no
    -- matter), its 'id' field is nullified by a response
    -- dispatcher.
    --
    function request_index:is_ready()
        return self.id == nil
    end
    --
    -- When a request is finished, a result can be got from a
    -- future object anytime.
    -- @retval result, nil Success, the response is returned.
    -- @retval nil, error Error occured.
    --
    function request_index:result()
        if self.errno then
            if type(self.response) ~= 'cdata' then
                -- Error could be set by the connection state
                -- machine, and it is usually a string explaining
                -- a reason.
                self.response = box.error.new({code = self.errno,
                                               reason = self.response})
            end
            return nil, self.response
        elseif not self.id then
            return self.response
        else
            return nil, box.error.new(box.error.PROC_LUA,
                                      'Response is not ready')
        end
    end
    --
    -- Get the next message or the final result.
    -- @param iterator Iterator object.
    -- @param i Index to get a next message from.
    --
    -- @retval nil, nil The request is finished.
    -- @retval i + 1, object A message/response and its index.
    -- @retval box.NULL, error An error occured. When this
    --         function is called in 'for k, v in future:pairs()',
    --         `k` becomes box.NULL, and `v` becomes error object.
    --         On error the key becomes exactly box.NULL instead
    --         of nil, because nil is treated by Lua as iteration
    --         end marker. Nil does not participate in iteration,
    --         and does not allow to continue it.
    --
    local function request_iterator_next(iterator, i)
        if i == box.NULL then
            return nil, nil
        else
            i = i + 1
        end
        local request = iterator.request
        local messages = request.on_push_ctx
    ::retry::
        if i <= #messages then
            return i, messages[i]
        end
        if request:is_ready() then
            -- After all the messages are iterated, `i` is equal
            -- to #messages + 1. After response reading `i`
            -- becomes #messages + 2. It is the trigger to finish
            -- the iteration.
            if i > #messages + 1 then
                return nil, nil
            end
            local response, err = request:result()
            if err then
                return box.NULL, err
            end
            return i, response
        end
        local old_message_count = #messages
        local timeout = iterator.timeout
        repeat
            local ts = fiber_clock()
            request.cond:wait(timeout)
            timeout = timeout - (fiber_clock() - ts)
            if request:is_ready() or old_message_count ~= #messages then
                goto retry
            end
        until timeout <= 0
        return box.NULL, box.error.new(E_TIMEOUT)
    end
    --
    -- Iterate over all messages, received by a request. @Sa
    -- request_iterator_next for details what to expect in `for`
    -- key/value pairs.
    -- @param timeout One iteration timeout.
    -- @retval next() callback, iterator, zero key.
    --
    function request_index:pairs(timeout)
        if timeout then
            if type(timeout) ~= 'number' or timeout < 0 then
                error('Usage: future:pairs(timeout)')
            end
        else
            timeout = TIMEOUT_INFINITY
        end
        local iterator = {request = self, timeout = timeout}
        return request_iterator_next, iterator, 0
    end
    --
    -- Wait for a response or error max timeout seconds.
    -- @param timeout Max seconds to wait.
    -- @retval result, nil Success, the response is returned.
    -- @retval nil, error Error occured.
    --
    function request_index:wait_result(timeout)
        if timeout then
            if type(timeout) ~= 'number' or timeout < 0 then
                error('Usage: future:wait_result(timeout)')
            end
        else
            timeout = TIMEOUT_INFINITY
        end
        if not self:is_ready() then
            -- When a response is ready before timeout, the
            -- waiting client is waked up prematurely.
            while timeout > 0 and not self:is_ready() do
                local ts = fiber.clock()
                self.cond:wait(timeout)
                timeout = timeout - (fiber.clock() - ts)
            end
            if not self:is_ready() then
                return nil, box.error.new(E_TIMEOUT)
            end
        end
        return self:result()
    end
    --
    -- Make a connection forget about the response. When it will
    -- be received, it will be ignored. It reduces size of
    -- requests table speeding up other requests.
    --
    function request_index:discard()
        if self.id then
            requests[self.id] = nil
            self.id = nil
            self.errno = box.error.PROC_LUA
            self.response = 'Response is discarded'
            self.cond:broadcast()
        end
    end

    local request_mt = { __index = request_index }

    -- STATE SWITCHING --
    local function set_state(new_state, new_errno, new_error)
        state = new_state
        last_errno = new_errno
        last_error = new_error
        callback('state_changed', new_state, new_error)
        state_cond:broadcast()
        if state == 'error' or state == 'error_reconnect' or
           state == 'closed' then
            for _, request in pairs(requests) do
                request.id = nil
                request.errno = new_errno
                request.response = new_error
                request.cond:broadcast()
            end
            requests = {}
        end
    end

    -- FYI: [] on a string is valid
    local function wait_state(target_state, timeout)
        local deadline = fiber_clock() + (timeout or TIMEOUT_INFINITY)
        repeat until state == target_state or target_state[state] or
                     is_final_state[state] or
                     not state_cond:wait(max(0, deadline - fiber_clock()))
        return state == target_state or target_state[state] or false
    end

    -- START/STOP --
    local protocol_sm

    local function start()
        if state ~= 'initial' then return not is_final_state[state] end
        fiber.create(function()
            local ok, err, timeout
            worker_fiber = fiber_self()
            fiber.name(string.format('%s:%s (net.box)', host, port), {truncate=true})
            if not connection then
                goto do_connect
            end
    ::handle_connection::
            ok, err = pcall(protocol_sm)
            if not (ok or is_final_state[state]) then
                set_state('error', E_UNKNOWN, err)
            end
            if connection then
                connection:close()
                connection = nil
            end
            timeout = callback('reconnect_timeout')
    ::do_reconnect::
            if not timeout or state ~= 'error_reconnect' then
                goto stop
            end
            fiber.sleep(timeout)
            timeout = callback('reconnect_timeout')
            if not timeout or state ~= 'error_reconnect' then
                goto stop
            end
    ::do_connect::
            connection, greeting =
                establish_connection(host, port, callback('fetch_connect_timeout'))
            if connection then
                goto handle_connection
            end
            timeout = callback('reconnect_timeout')
            if not timeout then
                set_state('error', E_NO_CONNECTION, greeting)
                goto stop
            end
            set_state('error_reconnect', E_NO_CONNECTION, greeting)
            goto do_reconnect
    ::stop::
            send_buf:recycle()
            recv_buf:recycle()
            worker_fiber = nil
        end)
    end

    local function stop()
        if not is_final_state[state] then
            set_state('closed', E_NO_CONNECTION, 'Connection closed')
        end
        if worker_fiber then
            worker_fiber:cancel()
            worker_fiber = nil
        end
    end

    --
    -- Send a request and do not wait for response.
    -- @retval nil, error Error occured.
    -- @retval not nil Future object.
    --
    local function perform_async_request(buffer, skip_header, method, on_push,
                                         on_push_ctx, format, ...)
        if state ~= 'active' and state ~= 'fetch_schema' then
            local code = last_errno or E_NO_CONNECTION
            local msg = last_error or
                string.format('Connection is not established, state is "%s"',
                              state)
            return nil, box.error.new({code = code, reason = msg})
        end
        -- alert worker to notify it of the queued outgoing data;
        -- if the buffer wasn't empty, assume the worker was already alerted
        if send_buf:size() == 0 then
            worker_fiber:wakeup()
        end
        local id = next_request_id
        encode_method(method, send_buf, id, ...)
        next_request_id = next_id(id)
        -- Request in most cases has maximum 10 members:
        -- method, buffer, skip_header, id, cond, errno, response,
        -- on_push, on_push_ctx and format.
        local request = setmetatable(table_new(0, 10), request_mt)
        request.method = method
        request.buffer = buffer
        request.skip_header = skip_header
        request.id = id
        request.cond = fiber.cond()
        requests[id] = request
        request.on_push = on_push
        request.on_push_ctx = on_push_ctx
        request.format = format
        return request
    end

    --
    -- Send a request and wait for response.
    -- @retval nil, error Error occured.
    -- @retval not nil Response object.
    --
    local function perform_request(timeout, buffer, skip_header, method,
                                   on_push, on_push_ctx, format, ...)
        local request, err =
            perform_async_request(buffer, skip_header, method, on_push,
                                  on_push_ctx, format, ...)
        if not request then
            return nil, err
        end
        return request:wait_result(timeout)
    end

    local function dispatch_response_iproto(hdr, body_rpos, body_end)
        local id = hdr[IPROTO_SYNC_KEY]
        local request = requests[id]
        if request == nil then -- nobody is waiting for the response
            return
        end
        local status = hdr[IPROTO_STATUS_KEY]
        local body_len = body_end - body_rpos

        if status > IPROTO_CHUNK_KEY then
            -- Handle errors
            requests[id] = nil
            request.id = nil
            request.errno = band(status, IPROTO_ERRNO_MASK)
            request.response = decode_error(body_rpos, request.errno)
            request.cond:broadcast()
            return
        end

        local buffer = request.buffer
        if buffer ~= nil then
            -- Copy xrow.body to user-provided buffer
            if request.skip_header then
                -- Skip {[IPROTO_DATA_KEY] = ...} wrapper.
                local map_len, key
                map_len, body_rpos = decode_map_header(body_rpos, body_len)
                assert(map_len == 1)
                key, body_rpos = decode(body_rpos)
                assert(key == IPROTO_DATA_KEY)
                body_len = body_end - body_rpos
            end
            local wpos = buffer:alloc(body_len)
            ffi.copy(wpos, body_rpos, body_len)
            body_len = tonumber(body_len)
            if status == IPROTO_OK_KEY then
                request.response = body_len
                requests[id] = nil
                request.id = nil
            else
                request.on_push(request.on_push_ctx, body_len)
            end
            request.cond:broadcast()
            return
        end

        local real_end
        -- Decode xrow.body[DATA] to Lua objects
        if status == IPROTO_OK_KEY then
            request.response, real_end = decode_method(request.method,
                                                       body_rpos, body_end,
                                                       request.format)
            assert(real_end == body_end, "invalid body length")
            requests[id] = nil
            request.id = nil
        else
            local msg
            msg, real_end, request.errno = decode_push(body_rpos, body_end)
            assert(real_end == body_end, "invalid body length")
            request.on_push(request.on_push_ctx, msg)
        end
        request.cond:broadcast()
    end

    local function dispatch_response_console(rid, response)
        local request = requests[rid]
        if request == nil then -- nobody is waiting for the response
            return
        end
        request.id = nil
        requests[rid] = nil
        request.response = response
        request.cond:broadcast()
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
        local required
        if data_len < 5 then
            required = 5
        else
            -- PWN! insufficient input validation
            local bufpos = recv_buf.rpos
            local len, rpos = decode(bufpos)
            required = (rpos - bufpos) + len
            if data_len >= required then
                local body_end = rpos + len
                local hdr, body_rpos = decode(rpos)
                recv_buf.rpos = body_end
                return nil, hdr, body_rpos, body_end
            end
        end
        local deadline = fiber_clock() + (timeout or TIMEOUT_INFINITY)
        local err, extra = send_and_recv(required, timeout)
        if err then
            return err, extra
        end
        return send_and_recv_iproto(max(0, deadline - fiber_clock()))
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

    --
    -- Protocol_sm is a core function of netbox. It calls all
    -- other ..._sm() functions, and explicitly or implicitly
    -- holds Lua referece on a connection object. It means, that
    -- until it works, the connection can not be garbage
    -- collected. See gh-3164, where because of reconnect sleeps
    -- in this function, a connection could not be deleted.
    --
    protocol_sm = function()
        assert(connection)
        assert(greeting)
        local err, msg = callback('handshake', greeting)
        if err then
            return error_sm(err, msg)
        end
        -- @deprecated since 1.10
        if greeting.protocol == 'Lua console' then
            log.warn("Netbox text protocol support is deprecated since 1.10, "..
                     "please use require('console').connect() instead")
            local setup_delimiter = 'require("console").delimiter("$EOF$")\n'
            encode_method(M_INJECT, send_buf, nil, setup_delimiter)
            local err, response = send_and_recv_console()
            if err then
                return error_sm(err, response)
            elseif response ~= '---\n...\n' then
                return error_sm(E_NO_CONNECTION, 'Unexpected response')
            end
            local rid = next_request_id
            set_state('active')
            return console_sm(rid)
        elseif greeting.protocol == 'Binary' then
            return iproto_auth_sm(greeting.salt)
        else
            return error_sm(E_NO_CONNECTION,
                            'Unknown protocol: '..greeting.protocol)
        end
    end

    console_sm = function(rid)
        local err, response = send_and_recv_console()
        if err then
            return error_sm(err, response)
        else
            dispatch_response_console(rid, response)
            return console_sm(next_id(rid))
        end
    end

    iproto_auth_sm = function(salt)
        set_state('auth')
        if not user or not password then
            set_state('fetch_schema')
            return iproto_schema_sm()
        end
        encode_auth(send_buf, new_request_id(), user, password, salt)
        local err, hdr, body_rpos = send_and_recv_iproto()
        if err then
            return error_sm(err, hdr)
        end
        if hdr[IPROTO_STATUS_KEY] ~= 0 then
            local body = decode(body_rpos)
            return error_sm(E_NO_CONNECTION, body[IPROTO_ERROR_24])
        end
        set_state('fetch_schema')
        return iproto_schema_sm(hdr[IPROTO_SCHEMA_VERSION_KEY])
    end

    iproto_schema_sm = function(schema_version)
        if not callback('will_fetch_schema') then
            set_state('active')
            return iproto_sm(schema_version)
        end
        -- _vcollation view was added in 2.2.0-389-g3e3ef182f
        local peer_has_vcollation = version_at_least(greeting.version_id,
                                                     2, 2, 1)
        local select1_id = new_request_id()
        local select2_id = new_request_id()
        local select3_id
        local response = {}
        -- fetch everything from space _vspace, 2 = ITER_ALL
        encode_method(M_SELECT, send_buf, select1_id, VSPACE_ID, 0, 2, 0,
                      0xFFFFFFFF, nil)
        -- fetch everything from space _vindex, 2 = ITER_ALL
        encode_method(M_SELECT, send_buf, select2_id, VINDEX_ID, 0, 2, 0,
                      0xFFFFFFFF, nil)
        -- fetch everything from space _vcollation, 2 = ITER_ALL
        if peer_has_vcollation then
            select3_id = new_request_id()
            encode_method(M_SELECT, send_buf, select3_id, VCOLLATION_ID,
                          0, 2, 0, 0xFFFFFFFF, nil)
        end

        schema_version = nil -- any schema_version will do provided that
                             -- it is consistent across responses
        repeat
            local err, hdr, body_rpos, body_end = send_and_recv_iproto()
            if err then return error_sm(err, hdr) end
            dispatch_response_iproto(hdr, body_rpos, body_end)
            local id = hdr[IPROTO_SYNC_KEY]
            -- trick: omit check for peer_has_vcollation: id is
            -- not nil
            if id == select1_id or id == select2_id or id == select3_id then
                -- response to a schema query we've submitted
                local status = hdr[IPROTO_STATUS_KEY]
                local response_schema_version = hdr[IPROTO_SCHEMA_VERSION_KEY]
                if status ~= 0 then
                    -- No _vcollation space (server has an old
                    -- schema version).
                    local errno = band(status, IPROTO_ERRNO_MASK)
                    if id == select3_id and errno == E_NO_SUCH_SPACE then
                        peer_has_vcollation = false
                        goto continue
                    end
                    local body = decode(body_rpos)
                    return error_sm(E_NO_CONNECTION, body[IPROTO_ERROR_24])
                end
                if schema_version == nil then
                    schema_version = response_schema_version
                elseif schema_version ~= response_schema_version then
                    -- schema changed while fetching schema; restart loader
                    return iproto_schema_sm()
                end
                local body = decode(body_rpos)
                response[id] = body[IPROTO_DATA_KEY]
            end
            ::continue::
        until response[select1_id] and response[select2_id] and
              (not peer_has_vcollation or response[select3_id])
        -- trick: response[select3_id] is nil when the key is nil
        callback('did_fetch_schema', schema_version, response[select1_id],
                 response[select2_id], response[select3_id])
        set_state('active')
        return iproto_sm(schema_version)
    end

    iproto_sm = function(schema_version)
        local err, hdr, body_rpos, body_end = send_and_recv_iproto()
        if err then return error_sm(err, hdr) end
        dispatch_response_iproto(hdr, body_rpos, body_end)
        local response_schema_version = hdr[IPROTO_SCHEMA_VERSION_KEY]
        if response_schema_version > 0 and
           response_schema_version ~= schema_version then
            -- schema_version has been changed - start to load a new version.
            -- Sic: self.schema_version will be updated only after reload.
            set_state('fetch_schema')
            return iproto_schema_sm(schema_version)
        end
        return iproto_sm(schema_version)
    end

    error_sm = function(err, msg)
        if connection then connection:close(); connection = nil end
        send_buf:recycle()
        recv_buf:recycle()
        if state ~= 'closed' then
            if callback('reconnect_timeout') then
                set_state('error_reconnect', err, msg)
            else
                set_state('error', err, msg)
            end
        end
    end

    return {
        stop            = stop,
        start           = start,
        wait_state      = wait_state,
        perform_request = perform_request,
        perform_async_request = perform_async_request,
    }
end

-- Wrap create_transport, adding auto-stop-on-GC feature.
-- All the GC magic is neatly encapsulated!
-- The tricky part is the callback:
--  * callback (typically) references the transport (indirectly);
--  * worker fiber references the callback;
--  * fibers are GC roots - i.e. transport is never GC-ed!
-- We solve the issue by making the worker->callback ref weak.
-- Now it is necessary to have a strong ref to callback somewhere or
-- it is GC-ed prematurely. We wrap stop() method, stashing the
-- ref in an upvalue (stop() performance doesn't matter much.)
local create_transport = function(host, port, user, password, callback,
                                  connection, greeting)
    local weak_refs = setmetatable({callback = callback}, {__mode = 'v'})
    local function weak_callback(...)
        local callback = weak_refs.callback
        if callback then return callback(...) end
    end
    local transport = create_transport(host, port, user, password,
                                       weak_callback, connection, greeting)
    local transport_stop = transport.stop
    local gc_hook = ffi.gc(ffi.new('char[1]'), function()
        pcall(transport_stop)
    end)
    transport.stop = function()
        -- dummy gc_hook, callback refs prevent premature GC
        return transport_stop(gc_hook, callback)
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
        schema_version = self.schema_version,
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

local function new_sm(host, port, opts, connection, greeting)
    local user, password = opts.user, opts.password; opts.password = nil
    local last_reconnect_error
    local remote = {host = host, port = port, opts = opts, state = 'initial'}
    local function callback(what, ...)
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
            return opts.connect_timeout or DEFAULT_CONNECT_TIMEOUT
        elseif what == 'did_fetch_schema' then
            remote:_install_schema(...)
        elseif what == 'reconnect_timeout' then
            if type(opts.reconnect_after) == 'number' and
               opts.reconnect_after > 0 then
                return opts.reconnect_after
            end
        end
    end
    -- @deprecated since 1.10
    if opts.console then
        log.warn("Netbox console API is deprecated since 1.10, please use "..
                 "require('console').connect() instead")
        setmetatable(remote, console_mt)
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
    remote._on_disconnect = trigger.new("on_disconnect")
    remote._on_connect = trigger.new("on_connect")
    remote._is_connected = false
    remote._transport = create_transport(host, port, user, password, callback,
                                         connection, greeting)
    remote._transport.start()
    if opts.wait_connected ~= false then
        remote._transport.wait_state('active', tonumber(opts.wait_connected))
    end
    return remote
end

--
-- Wrap an existing connection into net.box API.
-- @param connection Connected socket.
-- @param greeting Decoded greeting, received from a server.
-- @param host Hostname to which @a connection is established.
-- @param port TCP port to which @a connection is established.
-- @param opts Options like reconnect_after, connect_timeout,
--        wait_connected, login, password, ...
--
-- @retval Net.box object.
--
local function wrap(connection, greeting, host, port, opts)
    if connection == nil or type(greeting) ~= 'table' then
        error('Usage: netbox.wrap(socket, greeting, [opts])')
    end
    opts = opts or {}
    return new_sm(host, port, opts, connection, greeting)
end

--
-- Connect to a remote server.
-- @param uri OR host and port. URI is a string like
--        hostname:port@login:password. Host and port can be
--        passed separately with login and password in the next
--        parameter.
-- @param opts @Sa wrap().
--
-- @retval Net.box object.
--
local function connect(...)
    return new_sm(parse_connect_params(...))
end

local function check_remote_arg(remote, method)
    if type(remote) ~= 'table' then
        local fmt = 'Use remote:%s(...) instead of remote.%s(...):'
        box.error(E_PROC_LUA, string.format(fmt, method, method))
    end
end

local function check_call_args(args)
    if args ~= nil and type(args) ~= 'table' then
        error("Use remote:call(func_name, {arg1, arg2, ...}, opts) "..
              "instead of remote:call(func_name, arg1, arg2, ...)")
    end
end

local function check_eval_args(args)
    if args ~= nil and type(args) ~= 'table' then
        error("Use remote:eval(expression, {arg1, arg2, ...}, opts) "..
              "instead of remote:eval(expression, arg1, arg2, ...)")
    end
end

function remote_methods:close()
    check_remote_arg(self, 'close')
    self._transport.stop()
end

function remote_methods:on_schema_reload(...)
    check_remote_arg(self, 'on_schema_reload')
    return self._on_schema_reload(...)
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
    return self._transport.wait_state('active', timeout)
end

function remote_methods:_request(method, opts, format, ...)
    local transport = self._transport
    local on_push, on_push_ctx, buffer, skip_header, deadline
    -- Extract options, set defaults, check if the request is
    -- async.
    if opts then
        buffer = opts.buffer
        skip_header = opts.skip_header
        if opts.is_async then
            if opts.on_push or opts.on_push_ctx then
                error('To handle pushes in an async request use future:pairs()')
            end
            local res, err =
                transport.perform_async_request(buffer, skip_header, method,
                                                table.insert, {}, format,
                                                ...)
            if err then
                box.error(err)
            end
            return res
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
    local timeout = deadline and max(0, deadline - fiber_clock())
    if self.state ~= 'active' then
        transport.wait_state('active', timeout)
        timeout = deadline and max(0, deadline - fiber_clock())
    end
    local res, err = transport.perform_request(timeout, buffer, skip_header,
                                               method, on_push, on_push_ctx,
                                               format, ...)
    if err then
        box.error(err)
    end
    -- Try to wait until a schema is reloaded if needed.
    -- Regardless of reloading result, the main response is
    -- returned, since it does not depend on any schema things.
    if self.state == 'fetch_schema' then
        timeout = deadline and max(0, deadline - fiber_clock())
        transport.wait_state('active', timeout)
    end
    return res
end

function remote_methods:ping(opts)
    check_remote_arg(self, 'ping')
    return (pcall(self._request, self, M_PING, opts))
end

function remote_methods:reload_schema()
    check_remote_arg(self, 'reload_schema')
    self:ping()
end

-- @deprecated since 1.7.4
function remote_methods:call_16(func_name, ...)
    check_remote_arg(self, 'call')
    return (self:_request(M_CALL_16, nil, nil, tostring(func_name), {...}))
end

function remote_methods:call(func_name, args, opts)
    check_remote_arg(self, 'call')
    check_call_args(args)
    args = args or {}
    local res = self:_request(M_CALL_17, opts, nil, tostring(func_name), args)
    if type(res) ~= 'table' or opts and opts.is_async then
        return res
    end
    return unpack(res)
end

-- @deprecated since 1.7.4
function remote_methods:eval_16(code, ...)
    check_remote_arg(self, 'eval')
    return unpack((self:_request(M_EVAL, nil, nil, code, {...})))
end

function remote_methods:eval(code, args, opts)
    check_remote_arg(self, 'eval')
    check_eval_args(args)
    args = args or {}
    local res = self:_request(M_EVAL, opts, nil, code, args)
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
    return self:_request(M_EXECUTE, netbox_opts, nil, query, parameters or {},
                         sql_opts or {})
end

function remote_methods:prepare(query, parameters, sql_opts, netbox_opts) -- luacheck: no unused args
    check_remote_arg(self, "prepare")
    if type(query) ~= "string" then
        box.error(box.error.SQL_PREPARE, "expected string as SQL statement")
    end
    if sql_opts ~= nil then
        box.error(box.error.UNSUPPORTED, "prepare", "options")
    end
    return self:_request(M_PREPARE, netbox_opts, nil, query)
end

function remote_methods:unprepare(query, parameters, sql_opts, netbox_opts)
    check_remote_arg(self, "unprepare")
    if type(query) ~= "number" then
        box.error("query id is expected to be numeric")
    end
    if sql_opts ~= nil then
        box.error(box.error.UNSUPPORTED, "unprepare", "options")
    end
    return self:_request(M_UNPREPARE, netbox_opts, nil, query, parameters or {},
                         sql_opts or {})
end

function remote_methods:wait_state(state, timeout)
    check_remote_arg(self, 'wait_state')
    return self._transport.wait_state(state, timeout)
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

-- console methods
console_methods.close = remote_methods.close
console_methods.on_schema_reload = remote_methods.on_schema_reload
console_methods.on_disconnect = remote_methods.on_disconnect
console_methods.on_connect = remote_methods.on_connect
console_methods.is_connected = remote_methods.is_connected
console_methods.wait_state = remote_methods.wait_state
function console_methods:eval(line, timeout)
    check_remote_arg(self, 'eval')
    local err, res
    local transport = self._transport
    local pr = transport.perform_request
    if self.state ~= 'active' then
        local deadline = fiber_clock() + (timeout or TIMEOUT_INFINITY)
        transport.wait_state('active', timeout)
        timeout = max(0, deadline - fiber_clock())
    end
    if self.protocol == 'Binary' then
        local loader = 'return require("console").eval(...)'
        res, err = pr(timeout, nil, false, M_EVAL, nil, nil, nil, loader,
                      {line})
    else
        assert(self.protocol == 'Lua console')
        res, err = pr(timeout, nil, false, M_INJECT, nil, nil, nil,
                      line..'$EOF$\n')
    end
    if err then
        box.error(err)
    end
    return res[1] or res
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
        return remote:_request(M_INSERT, opts, self._format_cdata, self.id,
                               tuple)
    end

    function methods:replace(tuple, opts)
        check_space_arg(self, 'replace')
        return remote:_request(M_REPLACE, opts, self._format_cdata, self.id,
                               tuple)
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
        return nothing_or_data(remote:_request(M_UPSERT, opts, nil, self.id,
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
        local key_is_nil = (key == nil or
                            (type(key) == 'table' and #key == 0))
        local iterator = check_iterator_type(opts, key_is_nil)
        local offset = tonumber(opts and opts.offset) or 0
        local limit = tonumber(opts and opts.limit) or 0xFFFFFFFF
        return (remote:_request(M_SELECT, opts, self.space._format_cdata,
                                self.space.id, self.id, iterator, offset,
                                limit, key))
    end

    function methods:get(key, opts)
        check_index_arg(self, 'get')
        if opts and opts.buffer then
            error("index:get() doesn't support `buffer` argument")
        end
        return nothing_or_data(remote:_request(M_GET, opts,
                                               self.space._format_cdata,
                                               self.space.id, self.id,
                                               box.index.EQ, 0, 2, key))
    end

    function methods:min(key, opts)
        check_index_arg(self, 'min')
        if opts and opts.buffer then
            error("index:min() doesn't support `buffer` argument")
        end
        return nothing_or_data(remote:_request(M_MIN, opts,
                                               self.space._format_cdata,
                                               self.space.id, self.id,
                                               box.index.GE, 0, 1, key))
    end

    function methods:max(key, opts)
        check_index_arg(self, 'max')
        if opts and opts.buffer then
            error("index:max() doesn't support `buffer` argument")
        end
        return nothing_or_data(remote:_request(M_MAX, opts,
                                               self.space._format_cdata,
                                               self.space.id, self.id,
                                               box.index.LE, 0, 1, key))
    end

    function methods:count(key, opts)
        check_index_arg(self, 'count')
        if opts and opts.buffer then
            error("index:count() doesn't support `buffer` argument")
        end
        local code = string.format('box.space.%s.index.%s:count',
                                   self.space.name, self.name)
        return remote:_request(M_COUNT, opts, nil, code, { key, opts })
    end

    function methods:delete(key, opts)
        check_index_arg(self, 'delete')
        return nothing_or_data(remote:_request(M_DELETE, opts,
                                               self.space._format_cdata,
                                               self.space.id, self.id, key))
    end

    function methods:update(key, oplist, opts)
        check_index_arg(self, 'update')
        return nothing_or_data(remote:_request(M_UPDATE, opts,
                                               self.space._format_cdata,
                                               self.space.id, self.id, key,
                                               oplist))
    end

    return { __index = methods, __metatable = false }
end

local this_module = {
    create_transport = create_transport,
    connect = connect,
    new = connect, -- Tarantool < 1.7.1 compatibility,
    wrap = wrap,
    establish_connection = establish_connection,
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
        inject      = M_INJECT,
    }
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
    call = function(_box, proc_name, args)
        check_remote_arg(_box, 'call')
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
    eval = function(_box, expr, args)
        check_remote_arg(_box, 'eval')
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
