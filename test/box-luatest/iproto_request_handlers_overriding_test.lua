local server = require('luatest.server')
local t = require('luatest')

-- Keep in sync with `is_iproto_override_supported' in `iproto.cc'.
local unsupported_rq_types = {
    JOIN = box.iproto.type.JOIN,
    SUBSCRIBE = box.iproto.type.SUBSCRIBE,
    FETCH_SNAPSHOT = box.iproto.type.FETCH_SNAPSHOT,
    REGISTER = box.iproto.type.REGISTER,
}

-- Grep server logs for error messages about unsupported request types.
local function check_unsupported_rq_types(cg)
    local msg
    for req in pairs(unsupported_rq_types) do
        msg = "C> IPROTO request handler overriding does not support `" ..
              req .. "' request type"
        t.assert(cg.server:grep_log(msg))
    end
end

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new{
        alias   = 'dflt',
    }
    cg.server:start()
    cg.server:exec(function(net_box_uri)
        local msgpack = require('msgpack')
        local uri = require('uri')
        local socket = require('socket')

        -- Connect to the server.
        local u = uri.parse(net_box_uri)
        local s = socket.tcp_connect(u.host, u.service)
        t.assert_is_not(s, nil)
        -- Skip the greeting.
        t.assert_equals(#s:read(128), 128)
        rawset(_G, 's', s)

        rawset(_G, 'header_is_ok', false)
        rawset(_G, 'body_is_ok', false)
        rawset(_G, 'body_is_empty', false)
        local resp = {0}
        rawset(_G, 'cb',
               function(header, body)
                    _G.header_is_ok = msgpack.is_object(header) and
                            header.sync == 1 and
                            header.SPACE_ID == 2 and
                            header[box.iproto.key.INDEX_ID] == 3
                    _G.body_is_ok = msgpack.is_object(body) and
                            body.options == 3 and
                            body.STMT_ID == 4 and
                            body[box.iproto.key.SQL_TEXT] == 'text'
                    _G.body_is_empty = next(body:decode()) == nil
                    box.iproto.send(box.session.id(), resp)
                    return true
        end)
        rawset(_G, 'test_cb_resp',
               function(header, body)
                   local mp_header = msgpack.encode(header)
                   local mp_body = (body ~= nil and msgpack.encode(body)) or ''
                   local mp_packet_len = msgpack.encode(#mp_header + #mp_body)
                   local mp = mp_packet_len .. mp_header .. mp_body
                   s:write(mp)
                   local mp_resp = msgpack.encode(resp)
                   local packet_size = 5 + #mp_resp
                   local packet = s:read(packet_size)
                   t.assert(_G.header_is_ok)
                   _G.header_is_ok = false
                   if body ~= nil then
                       t.assert(_G.body_is_ok)
                       _G.body_is_ok = false
                   else
                       t.assert(_G.body_is_empty)
                       _G.body_is_empty = false
                   end
                   t.assert_equals(#packet, packet_size)
                   local packet_len, next = msgpack.decode(packet)
                   t.assert_equals(packet_size - next + 1, packet_len)
                   local packet_header, next = msgpack.decode(packet, next)
                   t.assert_equals(next, packet_size + 1)
                   t.assert_equals(packet_header, resp)
               end)
        rawset(_G, 'test_cb_err',
               function(header, body, err_code, err_msg_pattern)
                   local mp_header = msgpack.encode(header)
                   local mp_body = (body ~= nil and msgpack.encode(body)) or ''
                   local mp_packet_len = msgpack.encode(#mp_header + #mp_body)
                   local mp = mp_packet_len .. mp_header .. mp_body
                   s:write(mp)
                   s:readable()
                   local packet = s:recv()
                   local packet_size = #packet
                   local packet_len, next = msgpack.decode(packet)
                   t.assert_equals(packet_size - next + 1, packet_len)
                   local packet_header, next = msgpack.decode(packet, next)
                   local packet_body, next = msgpack.decode(packet, next)
                   t.assert_equals(next, packet_size + 1)
                   t.assert_equals(packet_header[box.iproto.key.REQUEST_TYPE],
                                   bit.bor(box.iproto.type.TYPE_ERROR,
                                           err_code))
                   t.assert_str_matches(packet_body[box.iproto.key.ERROR_24],
                                        err_msg_pattern)
               end)
        rawset(_G, 'test_ping',
               function()
                   local header = setmetatable(
                    {
                        [box.iproto.key.REQUEST_TYPE] = box.iproto.type.PING,
                        [box.iproto.key.SYNC] = 1,
                    }, {__serialize = 'map'})
                    local mp_header = msgpack.encode(header)
                    local mp_packet_len = msgpack.encode(#mp_header)
                    local mp = mp_packet_len .. mp_header
                    s:write(mp)
                    local resp_header = setmetatable({
                        [box.iproto.key.REQUEST_TYPE] = box.iproto.type.OK,
                        [box.iproto.key.SYNC] = header[box.iproto.key.SYNC],
                        [box.iproto.key.SCHEMA_VERSION] =
                        box.info.schema_version,
                    }, {__serialize = 'map'})
                    local resp_body = setmetatable({}, {__serialize = 'map'})
                    local packet_size = 33
                    local packet = s:read(packet_size)
                    local packet_len, next = msgpack.decode(packet)
                    t.assert_equals(packet_len, packet_size - 5)
                    t.assert_equals(packet_size - next + 1, packet_len)
                    local packet_header, next = msgpack.decode(packet, next)
                    t.assert_equals(packet_size - next + 1, packet_size - 32)
                    local packet_body, next = msgpack.decode(packet, next)
                    t.assert_equals(next, packet_size + 1)
                    t.assert_equals(packet_header, resp_header)
                    t.assert_equals(packet_body, resp_body)
               end)
    end, {cg.server.net_box_uri})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Checks that `box.iproto.override` raises an error if called on
-- an unconfigured instance (gh-8975).
g.test_box_iproto_override_without_cfg = function()
    t.assert_error_msg_equals('Please call box.cfg{} first',
                              box.iproto.override, box.iproto.type.UNKNOWN,
                              function() end)
end

-- Checks that `box.iproto.override` errors are handled correctly.
g.test_box_iproto_override_errors = function(cg)
    cg.server:exec(function(unsupported_rq_types)
        local err_msg = "Usage: box.iproto.override(request_type, callback)"
        t.assert_error_msg_content_equals(err_msg, function()
            box.iproto.override()
        end)
        t.assert_error_msg_content_equals(err_msg, function()
            box.iproto.override(0)
        end)
        t.assert_error_msg_content_equals(err_msg, function()
            box.iproto.override(0, function() end, 'str')
        end)
        err_msg = "expected uint64_t as 1 argument"
        t.assert_error_msg_content_equals(err_msg, function()
            box.iproto.override('str', function() end)
        end)
        err_msg = "bad argument #2 to 'override' " ..
                  "(function expected, got string)"
        t.assert_error_msg_content_equals(err_msg, function()
            box.iproto.override(0, 'str')
        end)
        for rq_name, rq_type in pairs(unsupported_rq_types) do
            err_msg = ("IPROTO request handler overriding does not " ..
                       "support %s request type"):format(rq_name)
            t.assert_error_msg_content_equals(err_msg, function()
                box.iproto.override(rq_type, function() end)
            end)
        end
    end, {unsupported_rq_types})
end

-- Checks that `box.iproto.override` reset of non-existing request handler is
-- handled correctly.
g.test_box_iproto_override_non_existing_request = function(cg)
    cg.server:exec(function()
        box.iproto.override(777, nil)
    end)
end

-- Checks that `box.iproto.override` works correctly for basic request types.
g.test_box_iproto_override_basic_rq_types = function(cg)
    cg.server:exec(function()
        local header = setmetatable(
                    {
                        [box.iproto.key.SYNC] = 1,
                        [box.iproto.key.SPACE_ID] = 2,
                        [box.iproto.key.INDEX_ID] = 3,
                    }, {__serialize = 'map'})
        local body = setmetatable(
                    {
                        [box.iproto.key.OPTIONS] = 3,
                        [box.iproto.key.STMT_ID] = 4,
                        [box.iproto.key.SQL_TEXT] = 'text'
                    }, {__serialize = 'map'})
        local rq_types = {
            box.iproto.type.SELECT,
            box.iproto.type.INSERT,
            box.iproto.type.REPLACE,
            box.iproto.type.UPDATE,
            box.iproto.type.DELETE,
            box.iproto.type.UPSERT,
            box.iproto.type.CALL_16,
            box.iproto.type.CALL,
            box.iproto.type.EVAL,
            box.iproto.type.WATCH,
            box.iproto.type.UNWATCH,
            box.iproto.type.EXECUTE,
            box.iproto.type.PREPARE,
            box.iproto.type.PING,
            box.iproto.type.ID,
            box.iproto.type.VOTE_DEPRECATED,
            box.iproto.type.VOTE,
            box.iproto.type.AUTH,
        }
        for _, rq_type in ipairs(rq_types) do
            box.iproto.override(rq_type, _G.cb)
            header[box.iproto.key.REQUEST_TYPE] = rq_type
            _G.test_cb_resp(header, body)
            _G.test_cb_resp(header)
            box.iproto.override(rq_type, nil)
        end
    end)
end

-- Checks that `box.iproto.override` works correctly for stream request types.
g.test_box_iproto_override_stream_rq_types = function(cg)
    cg.server:exec(function()
        local header = setmetatable(
                    {
                        [box.iproto.key.SYNC] = 1,
                        [box.iproto.key.SPACE_ID] = 2,
                        [box.iproto.key.INDEX_ID] = 3,
                    }, {__serialize = 'map'})
        local body = setmetatable(
                    {
                        [box.iproto.key.OPTIONS] = 3,
                        [box.iproto.key.STMT_ID] = 4,
                        [box.iproto.key.SQL_TEXT] = 'text'
                    }, {__serialize = 'map'})
        local rq_types = {
            box.iproto.type.BEGIN,
            box.iproto.type.COMMIT,
            box.iproto.type.ROLLBACK,
        }
        for _, rq_type in ipairs(rq_types) do
            box.iproto.override(rq_type, _G.cb)
            header[box.iproto.key.REQUEST_TYPE] = rq_type
            _G.test_cb_err(header, nil,
                           box.error.UNABLE_TO_PROCESS_OUT_OF_STREAM,
                           "Unable to process %u+ request out of stream")
            header[box.iproto.key.STREAM_ID] = 1
            _G.test_cb_resp(header, body)
            header[box.iproto.key.STREAM_ID] = 0
            box.iproto.override(rq_type, nil)
        end
    end)
end

-- Checks that `box.iproto.override` works correctly with IPROTO_NOP.
g.test_box_iproto_override_nop_rq_type = function(cg)
    cg.server:exec(function()
        box.iproto.override(box.iproto.type.NOP, _G.cb)
        local header = setmetatable(
                    {
                        [box.iproto.key.REQUEST_TYPE] = box.iproto.type.NOP,
                        [box.iproto.key.SYNC] = 1,
                        [box.iproto.key.SPACE_ID] = 2,
                        [box.iproto.key.INDEX_ID] = 3,
                    }, {__serialize = 'map'})
        local body = setmetatable(
                    {
                        [box.iproto.key.OPTIONS] = 3,
                        [box.iproto.key.STMT_ID] = 4,
                        [box.iproto.key.SQL_TEXT] = 'text'
                    }, {__serialize = 'map'})
        _G.test_cb_err(header, body, box.error.INVALID_MSGPACK,
                       "Invalid MsgPack %- packet body")
        box.iproto.override(box.iproto.type.NOP, nil)
    end)
end

-- Checks that `box.iproto.override` works correctly with arbitrary request
-- type.
g.test_box_iproto_override_arbitrary_rq_type = function(cg)
    cg.server:exec(function()
        local rq_type = 777
        box.iproto.override(rq_type, _G.cb)
        local header = setmetatable(
                    {
                        [box.iproto.key.REQUEST_TYPE] = rq_type,
                        [box.iproto.key.SYNC] = 1,
                        [box.iproto.key.SPACE_ID] = 2,
                        [box.iproto.key.INDEX_ID] = 3,
                    }, {__serialize = 'map'})
        local body = setmetatable(
                    {
                        [box.iproto.key.OPTIONS] = 3,
                        [box.iproto.key.STMT_ID] = 4,
                        [box.iproto.key.SQL_TEXT] = 'text'
                    }, {__serialize = 'map'})
        _G.test_cb_resp(header, body)
        box.iproto.override(rq_type, nil)
    end)
end

-- Checks that `box.iproto.override` works correctly with IPROTO_UNKNOWN and
-- unknown request type.
g.test_box_iproto_override_unknown_rq_type = function(cg)
    cg.server:exec(function()
        box.iproto.override(box.iproto.type.UNKNOWN, _G.cb)
        local header = setmetatable(
                    {
                        [box.iproto.key.REQUEST_TYPE] = 777,
                        [box.iproto.key.SYNC] = 1,
                        [box.iproto.key.SPACE_ID] = 2,
                        [box.iproto.key.INDEX_ID] = 3,
                    }, {__serialize = 'map'})
        local body = setmetatable(
                    {
                        [box.iproto.key.OPTIONS] = 3,
                        [box.iproto.key.STMT_ID] = 4,
                        [box.iproto.key.SQL_TEXT] = 'text'
                    }, {__serialize = 'map'})
        _G.test_cb_resp(header, body)

        -- Checks that unknown request type handler does not shadow other
        -- request handlers.
        _G.test_ping()

        local rq_type = 777
        box.iproto.override(rq_type, _G.cb)
        local header = setmetatable(
                    {
                        [box.iproto.key.REQUEST_TYPE] = rq_type,
                        [box.iproto.key.SYNC] = 1,
                        [box.iproto.key.SPACE_ID] = 2,
                        [box.iproto.key.INDEX_ID] = 3,
                    }, {__serialize = 'map'})
        local body = setmetatable(
                    {
                        [box.iproto.key.OPTIONS] = 3,
                        [box.iproto.key.STMT_ID] = 4,
                        [box.iproto.key.SQL_TEXT] = 'text'
                    }, {__serialize = 'map'})
        _G.test_cb_resp(header, body)
        box.iproto.override(rq_type, nil)

        box.iproto.override(box.iproto.type.UNKNOWN, nil)
    end)
end

-- Checks that `box.iproto.override` works correctly with callback that falls
-- back to system handler.
g.test_box_iproto_override_cb_fallback = function(cg)
    cg.server:exec(function()
        local cb_fallback = function()
            return false
        end
        box.iproto.override(box.iproto.type.PING, cb_fallback)
        _G.test_ping()
        box.iproto.override(box.iproto.type.PING, nil)
    end)
end

-- Checks that `box.iproto.override` works correctly with callback throwing
-- `box.error`.
g.test_box_iproto_override_cb_diag_err = function(cg)
    cg.server:exec(function()
        local cb_diag_err = function()
            box.error{reason = 'test', code = 777}
        end
        box.iproto.override(box.iproto.type.PING, cb_diag_err)
        local header = setmetatable(
                    {
                        [box.iproto.key.REQUEST_TYPE] = box.iproto.type.PING,
                        [box.iproto.key.SYNC] = 1,
                    }, {__serialize = 'map'})
        _G.test_cb_err(header, nil, 777, "test")
        box.iproto.override(box.iproto.type.PING, nil)
    end)
end

-- Checks that `box.iproto.override` works correctly with callback throwing Lua
-- error.
g.test_box_iproto_override_cb_lua_err = function(cg)
    cg.server:exec(function()
        local cb_lua_err = function()
            error('test')
        end
        box.iproto.override(box.iproto.type.PING, cb_lua_err)
        local header = setmetatable(
                {
                    [box.iproto.key.REQUEST_TYPE] = box.iproto.type.PING,
                    [box.iproto.key.SYNC] = 1,
                }, {__serialize = 'map'})
        _G.test_cb_err(header, nil, box.error.PROC_LUA, ".+: test")
        box.iproto.override(box.iproto.type.PING, nil)
    end)
end

-- Checks that `box.iproto.override` works correctly with Lua callback with
-- invalid return type.
g.test_box_iproto_override_cb_lua_invalid_return_type = function(cg)
    cg.server:exec(function()
        local cb_lua_invalid_return_type = function()
            return box.NULL
        end
        box.iproto.override(box.iproto.type.PING, cb_lua_invalid_return_type)
        local header = setmetatable(
                {
                    [box.iproto.key.REQUEST_TYPE] = box.iproto.type.PING,
                    [box.iproto.key.SYNC] = 1,
                }, {__serialize = 'map'})
        _G.test_cb_err(header, nil, box.error.PROC_LUA,
                       "Invalid Lua IPROTO handler return type 'cdata' " ..
                       "%(expected boolean%)")
        box.iproto.override(box.iproto.type.PING, nil)
    end)
end

-- gh-9345: Checks that we don't account message twice in case of fallback.
g.test_box_iproto_override_fallback_double_accounting = function(cg)
    cg.server:exec(function()
        local cb_fallback = function()
            return false
        end
        local before = box.stat.net().REQUESTS_IN_PROGRESS.current
        box.iproto.override(box.iproto.type.PING, cb_fallback)
        _G.test_ping()
        box.iproto.override(box.iproto.type.PING, nil)
        local after = box.stat.net().REQUESTS_IN_PROGRESS.current
        t.assert_equals(after - before, 0)
    end)
end

-- Start server and set global functions.
local function init_server(cg, server_env)
    cg.server = server:new({env = server_env})
    cg.server:start()
    cg.server:exec(function(net_box_uri)
        rawset(_G, 'send_request_and_read_response', function(request_type)
            local uri = require('uri')
            local socket = require('socket')
            -- Connect to the server.
            local u = uri.parse(net_box_uri)
            local s = socket.tcp_connect(u.host, u.service)
            local greeting = s:read(box.iproto.GREETING_SIZE)
            greeting = box.iproto.decode_greeting(greeting)
            t.assert_covers(greeting, {protocol = 'Binary'})
            -- Send the request.
            local request = box.iproto.encode_packet(
                {request_type = request_type}, {}
            )
            t.assert_equals(s:write(request), #request)
            -- Read the response.
            local response = ''
            local header, body
            repeat
                header, body = box.iproto.decode_packet(response)
                if header == nil then
                    local size = body
                    local data = s:read(size)
                    t.assert_is_not(data)
                    response = response .. data
                end
            until header ~= nil
            s:close()
            return body[1]
        end)
    end, {cg.server.net_box_uri})
end

-- Delete spaces and triggers.
local function delete_spaces_and_triggers(cg)
    cg.server:exec(function()
        local trigger = require('trigger')
        if box.space.test then box.space.test:drop() end
        -- Delete all registered triggers.
        for event, trigger_list in pairs(trigger.info()) do
            for _, trigger_descr in pairs(trigger_list) do
                local trigger_name = trigger_descr[1]
                trigger.del(event, trigger_name)
            end
        end
    end)
end

-- Grep server logs for error messages about wrong request types.
-- Note that event names are case-sensitive.
local function check_wrong_rq_types(cg)
    local msg
    msg = "C> The event `box.iproto.override.' is in IPROTO override " ..
          "namespace, but `' is not a valid request type"
    t.assert(cg.server:grep_log(msg))
    msg = "C> The event `box.iproto.override.pinG' is in IPROTO override " ..
          "namespace, but `pinG' is not a valid request type"
    t.assert(cg.server:grep_log(msg))
    msg = "C> The event `box.iproto.override%[' is in IPROTO override " ..
          "namespace, but `%[' is not a valid request type"
    t.assert(cg.server:grep_log(msg))
    msg = "C> The event `box.iproto.override%[64' is in IPROTO override " ..
          "namespace, but `%[64' is not a valid request type"
    t.assert(cg.server:grep_log(msg))
    msg = "C> The event `box.iproto.override%[ 64%]' is in IPROTO override " ..
          "namespace, but `%[ 64%]' is not a valid request type"
    t.assert(cg.server:grep_log(msg))
    msg = "C> The event `box.iproto.override%[64 %]' is in IPROTO override " ..
          "namespace, but `%[64 %]' is not a valid request type"
    t.assert(cg.server:grep_log(msg))
    msg = "C> The event `box.iproto.override%[64%] ' is in IPROTO override " ..
          "namespace, but `%[64%] ' is not a valid request type"
    t.assert(cg.server:grep_log(msg))
    msg = "C> The event `box.iproto.override%[%-%]' is in IPROTO override " ..
          "namespace, but `%[%-%]' is not a valid request type"
    t.assert(cg.server:grep_log(msg))
    msg = "C> The event `box.iproto.override%[ping%]' is in IPROTO override " ..
          "namespace, but `%[ping%]' is not a valid request type"
    t.assert(cg.server:grep_log(msg))
    msg = "C> The event `box.iproto.override%[64%].ping' is in IPROTO " ..
          "override namespace, but `%[64%].ping' is not a valid request type"
    t.assert(cg.server:grep_log(msg))
end

-- Test IPROTO request handlers override using event triggers, that are set
-- before `box.cfg{}'. Requests are sent via a raw socket to test the response
-- on the unknown request types.
local g2 = t.group('gh-8138-triggers-before-box-cfg')

g2.before_all(function(cg)
    -- List of unsupported request types, represented as a string.
    local req_types_str = ''
    for req in pairs(unsupported_rq_types) do
        req_types_str = req_types_str .. ("'%s', "):format(req:lower())
    end
    -- Set event triggers before `box.cfg{}'.
    t.assert_equals(box.iproto.type.EXECUTE, 11)
    local run_before_cfg = ([[
        local trigger = require('trigger')
        local function handler_execute()
            local resp = 'IPROTO_EXECUTE handler, set by name, before box.cfg{}'
            box.iproto.send(box.session.id(), {}, {resp})
            return true
        end
        local function handler_n11()
            local resp = 'IPROTO_EXECUTE handler, set by id, before box.cfg{}'
            box.iproto.send(box.session.id(), {}, {resp})
            return true
        end
        local function handler_ping()
            local resp = 'IPROTO_PING handler, set by name, before box.cfg{}'
            box.iproto.send(box.session.id(), {}, {resp})
            return true
        end
        local function handler_n555()
            local resp = 'IPROTO #555 handler, set by id, before box.cfg{}'
            box.iproto.send(box.session.id(), {}, {resp})
            return true
        end
        local function handler_unknown()
            local resp = 'IPROTO_UNKNOWN handler, set by name, before box.cfg{}'
            box.iproto.send(box.session.id(), {}, {resp})
            return true
        end
        trigger.set('box.iproto.override.execute', 'exec', handler_execute)
        trigger.set('box.iproto.override[11]', '#11', handler_n11)
        trigger.set('box.iproto.override.ping', 'ping', handler_ping)
        trigger.set('box.iproto.override[555]', '#555', handler_n555)
        trigger.set('box.iproto.override.unknown', 'unk', handler_unknown)
        -- Set triggers on the unsupported request types.
        for _, req in pairs({%s}) do
            trigger.set('box.iproto.override.' .. req, 'unsup', function() end)
        end
        -- Set triggers on the wrong request types.
        trigger.set('box.iproto.override.', 'wrong', function() end)
        trigger.set('box.iproto.override.pinG', 'wrong', function() end)
        trigger.set('box.iproto.override[', 'wrong', function() end)
        trigger.set('box.iproto.override[64', 'wrong', function() end)
        trigger.set('box.iproto.override[ 64]', 'wrong', function() end)
        trigger.set('box.iproto.override[64 ]', 'wrong', function() end)
        trigger.set('box.iproto.override[64] ', 'wrong', function() end)
        trigger.set('box.iproto.override[-]', 'wrong', function() end)
        trigger.set('box.iproto.override[ping]', 'wrong', function() end)
        trigger.set('box.iproto.override[64].ping', 'wrong', function() end)
    ]]):format(req_types_str)
    init_server(cg, {['TARANTOOL_RUN_BEFORE_BOX_CFG'] = run_before_cfg})
end)

g2.after_all(function(cg) cg.server:drop() end)
g2.after_each(delete_spaces_and_triggers)

-- Check that it is possible to override the handlers using event triggers.
g2.test_event_triggers = function(cg)
    -- Grep logs for errors about unsupported and wrong request types.
    check_unsupported_rq_types(cg)
    check_wrong_rq_types(cg)

    -- Send correct requests with overridden handlers.
    cg.server:exec(function()
        -- Note that IPROTO_EXECUTE is overridden both by id and by name, and
        -- "by id" handler is always called first.
        t.assert_equals(
            _G.send_request_and_read_response(box.iproto.type.EXECUTE),
            'IPROTO_EXECUTE handler, set by id, before box.cfg{}')
        t.assert_equals(
            _G.send_request_and_read_response(box.iproto.type.PING),
            'IPROTO_PING handler, set by name, before box.cfg{}')
        -- Send an unknown request with a dedicated handler.
        t.assert_equals(
            _G.send_request_and_read_response(555),
            'IPROTO #555 handler, set by id, before box.cfg{}')
        -- Send an unknown request without a dedicated handler.
        t.assert_equals(
            _G.send_request_and_read_response(666),
            'IPROTO_UNKNOWN handler, set by name, before box.cfg{}')
    end)
end
