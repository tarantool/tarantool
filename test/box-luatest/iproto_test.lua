local msgpack = require('msgpack')
local server = require('luatest.server')
local socket = require('socket')
local t = require('luatest')
local uri = require('uri')
local uuid = require('uuid')

local g = t.group()
local iproto = box.iproto
local key, iproto_type = iproto.key, iproto.type
local timeout = 10
local empty_map = setmetatable({}, {__serialize = 'map'})

local function read_exact(sock, size)
    local data = sock:read(size, timeout)
    t.assert_equals(data and #data, size)
    return data
end

local function read_packet(sock)
    -- Tarantool sends packet lengths as MsgPack uint32 values.
    local prefix = read_exact(sock, 5)
    local payload = read_exact(sock, msgpack.decode(prefix))
    return iproto.decode_packet(prefix .. payload)
end

local function write(cg, data)
    t.assert_equals(cg.socket:write(data, timeout), #data)
end

local function encode_request(cg, request_type, body, header)
    cg.sync = cg.sync + 1
    header = header or {}
    header[key.REQUEST_TYPE] = request_type
    header[key.SYNC] = cg.sync
    return iproto.encode_packet(header, body), cg.sync
end

local function send_request(cg, request_type, body, header)
    local packet, sync = encode_request(cg, request_type, body, header)
    write(cg, packet)
    return sync
end

local function read_response(cg, sync)
    local header, body = read_packet(cg.socket)
    if sync ~= nil then
        t.assert_equals(header[key.SYNC], sync)
    end
    return header, body:decode()
end

local function request(cg, request_type, body, opts)
    opts = opts or {}
    local sync = send_request(cg, request_type, body, opts.header)
    local reply, result = read_response(cg, sync)
    local error_code = opts.error_code
    local code = error_code == nil and iproto_type.OK or
                 bit.bor(iproto_type.TYPE_ERROR, error_code)
    t.assert_equals(reply[key.REQUEST_TYPE], code, result[key.ERROR_24])
    if error_code ~= nil then
        t.assert_type(result[key.ERROR_24], 'string')
    end
    return result, reply
end

local function check_data(cg, request_type, body, expected)
    local result = request(cg, request_type, body)
    t.assert_equals(result[key.DATA], expected)
end

local function create_space(cg, engine, index_opts)
    return cg.server:exec(function(engine, index_opts)
        local s = box.schema.space.create('test', {engine = engine})
        s:create_index('pk', index_opts)
        box.schema.user.grant('guest', 'read,write', 'space', s.name)
        return s.id
    end, {engine, index_opts})
end

g.before_each(function(cg)
    cg.server = server:new{net_box_credentials = {user = 'admin'}}
    cg.server:start()
    local u = uri.parse(cg.server.net_box_uri)
    cg.socket = socket.tcp_connect(u.host, u.service, timeout)
    t.assert(cg.socket)
    local greeting = read_exact(cg.socket, iproto.GREETING_SIZE)
    t.assert(iproto.decode_greeting(greeting))
    cg.sync = 0
end)

g.after_each(function(cg)
    if cg.socket ~= nil then
        cg.socket:close()
        cg.socket = nil
    end
    cg.server:drop()
end)

g.test_dml_pipeline = function(cg)
    local space_id = create_space(cg, 'memtx')
    local tuples = {{1, 'red'}, {2, 'green'}, {3, 'blue'}}
    local packets, pending = {}, {}
    local function queue(request_type, tuple)
        local body = {[key.SPACE_ID] = space_id}
        if request_type == iproto_type.INSERT then
            body[key.TUPLE] = tuple
        else
            body[key.KEY] = {tuple[1]}
            body[key.LIMIT] = 1
        end
        local packet, sync = encode_request(cg, request_type, body)
        packets[#packets + 1] = packet
        pending[sync] = tuple
    end
    local function check_batch()
        write(cg, table.concat(packets))
        -- SYNC, not arrival order, identifies each response in the pipeline.
        for _ = 1, #packets do
            local header, body = read_response(cg)
            t.assert_equals(header[key.REQUEST_TYPE], iproto_type.OK)
            local sync = header[key.SYNC]
            t.assert(pending[sync])
            t.assert_equals(body[key.DATA], {pending[sync]})
            pending[sync] = nil
        end
        packets = {}
    end

    for _, tuple in ipairs(tuples) do
        queue(iproto_type.INSERT, tuple)
    end
    check_batch()
    check_data(cg, iproto_type.SELECT, {
        [key.SPACE_ID] = space_id, [key.KEY] = {}, [key.LIMIT] = 100,
    }, tuples)

    local yellow, purple = {4, 'yellow'}, {5, 'purple'}
    queue(iproto_type.INSERT, yellow)
    queue(iproto_type.SELECT, tuples[1])
    check_batch()

    queue(iproto_type.SELECT, tuples[2])
    queue(iproto_type.INSERT, purple)
    check_batch()

    queue(iproto_type.SELECT, yellow)
    queue(iproto_type.SELECT, purple)
    check_batch()
end

g.test_msgpack_strings = function(cg)
    local space_id = create_space(cg, 'memtx', {
        type = 'hash', parts = {{1, 'string'}},
    })
    -- Hash lookups must match keys even when the stored string and the
    -- lookup key use different valid MsgPack string headers.
    local cases = {
        {1, {'\xa1', '\xd9\x01', '\xda\x00\x01', '\xdb\x00\x00\x00\x01'}},
        {31, {'\xbf', '\xd9\x1f', '\xda\x00\x1f', '\xdb\x00\x00\x00\x1f'}},
        {32, {'\xd9\x20', '\xda\x00\x20', '\xdb\x00\x00\x00\x20'}},
        {255, {'\xd9\xff', '\xda\x00\xff', '\xdb\x00\x00\x00\xff'}},
        {256, {'\xda\x01\x00', '\xdb\x00\x00\x01\x00'}},
        {65535, {'\xda\xff\xff', '\xdb\x00\x00\xff\xff'}},
        {65536, {'\xdb\x00\x01\x00\x00'}},
    }
    for _, case in ipairs(cases) do
        local size, prefixes = unpack(case)
        local field = string.rep('x', size)
        local encodings = {}
        for i, prefix in ipairs(prefixes) do
            encodings[i] = msgpack.object_from_raw('\x91' .. prefix .. field)
        end
        local expected = {{field}}
        for _, tuple in ipairs(encodings) do
            check_data(cg, iproto_type.INSERT, {
                [key.SPACE_ID] = space_id, [key.TUPLE] = tuple,
            }, expected)
            for _, mp_key in ipairs(encodings) do
                check_data(cg, iproto_type.SELECT, {
                    [key.SPACE_ID] = space_id, [key.KEY] = mp_key,
                    [key.LIMIT] = 1,
                }, expected)
            end
            check_data(cg, iproto_type.DELETE, {
                [key.SPACE_ID] = space_id, [key.KEY] = {field},
            }, expected)
        end
    end
end

g.test_select_schema_version = function(cg)
    local space_id = create_space(cg, 'memtx')
    local select_body = {
        [key.SPACE_ID] = space_id, [key.KEY] = {}, [key.LIMIT] = 1,
    }
    -- An absent or zero schema version skips the version check.
    local body, header = request(cg, iproto_type.SELECT, select_body)
    t.assert_equals(body[key.DATA], {})
    local version = header[key.SCHEMA_VERSION]
    t.assert_gt(version, 0)
    for _, requested_version in ipairs({0, version}) do
        body, header = request(cg, iproto_type.SELECT, select_body, {
            header = {[key.SCHEMA_VERSION] = requested_version},
        })
        t.assert_equals(body[key.DATA], {})
        t.assert_equals(header[key.SCHEMA_VERSION], version)
    end
    body, header = request(cg, iproto_type.SELECT, select_body, {
        error_code = box.error.WRONG_SCHEMA_VERSION,
        header = {[key.SCHEMA_VERSION] = version + 1},
    })
    t.assert_equals(header[key.SCHEMA_VERSION], version)

    local new_version = cg.server:exec(function()
        box.schema.space.create('another_space')
        return box.info.schema_version
    end)
    t.assert_gt(new_version, version)
    body, header = request(cg, iproto_type.SELECT, select_body, {
        error_code = box.error.WRONG_SCHEMA_VERSION,
        header = {[key.SCHEMA_VERSION] = version},
    })
    t.assert_equals(header[key.SCHEMA_VERSION], new_version)
end

g.test_join_sync = function(cg)
    local space_id = create_space(cg, 'memtx')
    cg.server:exec(function()
        box.space.test:insert{1, 'data'}
        box.schema.user.grant('guest', 'replication')
    end)
    local sync = send_request(cg, iproto_type.JOIN, {
        [key.INSTANCE_UUID] = uuid.str(),
    })
    -- Three OK/vclock markers bracket the snapshot and WAL streams.
    -- All packets in both streams echo the JOIN request's SYNC.
    local markers, saw_tuple = 0, false
    while markers < 3 do
        local header, body = read_response(cg, sync)
        local code = header[key.REQUEST_TYPE]
        if code == iproto_type.OK then
            t.assert_type(body[key.VCLOCK], 'table')
            markers = markers + 1
        else
            t.assert_lt(code, iproto_type.TYPE_ERROR)
            if code == iproto_type.INSERT and
               body[key.SPACE_ID] == space_id then
                t.assert_equals(body[key.TUPLE], {1, 'data'})
                saw_tuple = true
            end
        end
    end
    t.assert(saw_tuple)

    local body = request(cg, iproto_type.JOIN, {
        [key.INSTANCE_UUID] = 'not-a-uuid',
    }, {error_code = box.error.INVALID_MSGPACK})
    t.assert_equals(body[key.ERROR_24], 'Invalid MsgPack - instance UUID')
end

g.test_vinyl_select = function(cg)
    local space_id = create_space(cg, 'vinyl')
    local tuples = cg.server:exec(function()
        local tuples = {{1}, {2, 'Music'}, {3, 'Length', 93}}
        for _, tuple in ipairs(tuples) do
            box.space.test:insert(tuple)
        end
        return tuples
    end)
    check_data(cg, iproto_type.SELECT, {
        [key.SPACE_ID] = space_id, [key.KEY] = {}, [key.LIMIT] = 10,
    }, tuples)
    check_data(cg, iproto_type.SELECT, {
        [key.SPACE_ID] = space_id, [key.KEY] = {2}, [key.LIMIT] = 1,
    }, {tuples[2]})
end

g.test_request_errors_keep_connection_open = function(cg)
    write(cg, iproto.encode_packet({'header'}))
    local header, body = read_response(cg, 0)
    local code = bit.bor(iproto_type.TYPE_ERROR, box.error.INVALID_MSGPACK)
    t.assert_equals(header[key.REQUEST_TYPE], code)
    t.assert_equals(body[key.ERROR_24], 'Invalid MsgPack - packet header')
    request(cg, iproto_type.PING)

    body = request(cg, iproto_type.SELECT, {
        [key.KEY] = {}, [key.LIMIT] = 1,
    }, {error_code = box.error.MISSING_REQUEST_FIELD})
    t.assert_equals(body[key.ERROR_24],
                    "Missing mandatory field 'SPACE_ID' in request")
    body = request(cg, iproto_type.SELECT, {
        [key.SPACE_ID] = box.schema.USER_ID, [key.KEY] = {}, [key.LIMIT] = 1,
    }, {error_code = box.error.ACCESS_DENIED})
    t.assert_equals(body[key.ERROR_24],
                    "Read access to space '_user' is denied for user 'guest'")
    request(cg, iproto_type.PING)
end

g.test_client_half_close_during_ping = function(cg)
    local packet = encode_request(cg, iproto_type.PING)
    -- Keep the declared length intact: the client stops mid-request.
    write(cg, packet:sub(1, -2))
    t.assert(cg.socket:shutdown('W'))
    t.assert_equals(cg.socket:read(1, timeout), '')
    t.assert(cg.server.net_box:ping({timeout = timeout}))
end

g.test_zero_based_update_and_upsert = function(cg)
    local space_id = create_space(cg, 'memtx')
    local tuple = {1, 0, 0, 0}
    -- The wire default is zero-based, unlike Lua field numbers.
    local ops = {{'+', 2, 1}, {'-', 3, 1}}
    check_data(cg, iproto_type.INSERT, {
        [key.SPACE_ID] = space_id, [key.TUPLE] = tuple,
    }, {tuple})
    check_data(cg, iproto_type.UPDATE, {
        [key.SPACE_ID] = space_id, [key.KEY] = {1}, [key.TUPLE] = ops,
    }, {{1, 0, 1, -1}})
    check_data(cg, iproto_type.UPSERT, {
        [key.SPACE_ID] = space_id, [key.TUPLE] = tuple,
        [key.INDEX_BASE] = 0, [key.OPS] = ops,
    }, {})
    check_data(cg, iproto_type.SELECT, {
        [key.SPACE_ID] = space_id, [key.KEY] = {1}, [key.LIMIT] = 1,
    }, {{1, 0, 2, -2}})
end

g.test_call_without_args = function(cg)
    cg.server:exec(function()
        box.schema.func.create('count_args', {
            body = "function(...) return select('#', ...) end",
        })
        box.schema.user.grant('guest', 'execute', 'function', 'count_args')
    end)
    local body = request(cg, iproto_type.CALL, {
        [key.FUNCTION_NAME] = 'count_args',
    })
    t.assert_equals(body[key.DATA], {0})
end

g.test_id = function(cg)
    for _, body in ipairs({
        {[key.VERSION] = 'abc'},
        {[key.FEATURES] = {'abc'}},
        {[key.AUTH_TYPE] = 123},
    }) do
        local result = request(cg, iproto_type.ID, body, {
            error_code = box.error.INVALID_MSGPACK,
        })
        t.assert_equals(result[key.ERROR_24], 'Invalid MsgPack - request body')
    end

    local features = {}
    for name in pairs(iproto.protocol_features) do
        features[#features + 1] = iproto.feature[name]
    end
    table.sort(features)
    local expected = {
        [key.VERSION] = iproto.protocol_version,
        [key.FEATURES] = features,
        [key.AUTH_TYPE] = 'chap-sha1',
    }
    -- Unknown versions and features do not change what the server advertises.
    local bodies = {
        empty_map,
        {[key.VERSION] = 99999999, [key.FEATURES] = {99999999}},
        {[0xdead] = 'unknown'},
    }
    for _, body in ipairs(bodies) do
        t.assert_equals(request(cg, iproto_type.ID, body), expected)
    end
end

local function check_invalid_watch_keys(cg, request_type)
    local body = request(cg, request_type, empty_map, {
        error_code = box.error.MISSING_REQUEST_FIELD,
    })
    t.assert_equals(body[key.ERROR_24],
                    "Missing mandatory field 'EVENT_KEY' in request")
    body = request(cg, request_type, {[key.EVENT_KEY] = 123}, {
        error_code = box.error.INVALID_MSGPACK,
    })
    t.assert_equals(body[key.ERROR_24], 'Invalid MsgPack - packet body')
end

g.test_watch = function(cg)
    local function watch(name)
        return send_request(cg, iproto_type.WATCH, {[key.EVENT_KEY] = name})
    end
    local function unwatch(name)
        send_request(cg, iproto_type.UNWATCH, {[key.EVENT_KEY] = name})
        -- UNWATCH has no reply; PING confirms it was processed.
        request(cg, iproto_type.PING)
    end
    local function check_event(sync, name, value)
        local header, body = read_response(cg, sync)
        t.assert_equals(header[key.REQUEST_TYPE], iproto_type.EVENT)
        t.assert_equals(body[key.EVENT_KEY], name)
        t.assert_equals(body[key.EVENT_DATA], value)
    end

    check_invalid_watch_keys(cg, iproto_type.WATCH)
    check_event(watch('foo'), 'foo', nil)
    check_event(watch('bar'), 'bar', nil)
    unwatch('bar')
    check_event(watch('bar'), 'bar', nil)

    cg.server:eval('box.broadcast(...)', {'foo', {1, 2, 3}})
    -- No EVENT until a subsequent WATCH acknowledges the previous one.
    request(cg, iproto_type.PING)
    check_event(watch('foo'), 'foo', {1, 2, 3})

    local sync = watch('bar')
    cg.server:eval('box.broadcast(...)', {'bar', 123})
    check_event(sync, 'bar', 123)
    watch('bar')
    unwatch('bar')
    cg.server:eval('box.broadcast(...)', {'bar', 456})
    request(cg, iproto_type.PING)
    check_event(watch('bar'), 'bar', 456)
end

g.test_watch_once = function(cg)
    local watch_body = {[key.EVENT_KEY] = 'foo'}
    local _, ping = request(cg, iproto_type.PING)
    local version = ping[key.SCHEMA_VERSION]
    t.assert_gt(version, 0)
    local body, header = request(cg, iproto_type.WATCH_ONCE, watch_body, {
        error_code = box.error.WRONG_SCHEMA_VERSION,
        header = {[key.SCHEMA_VERSION] = version + 1},
    })
    t.assert_equals(header[key.SCHEMA_VERSION], version)
    body, header = request(cg, iproto_type.WATCH_ONCE, watch_body, {
        header = {[key.SCHEMA_VERSION] = version},
    })
    t.assert_equals(header[key.SCHEMA_VERSION], version)
    t.assert_equals(body[key.DATA], {})
    body = request(cg, iproto_type.WATCH_ONCE, watch_body, {
        error_code = box.error.UNABLE_TO_PROCESS_IN_STREAM,
        header = {[key.STREAM_ID] = 1},
    })
    t.assert_equals(body[key.ERROR_24],
                    'Unable to process WATCH_ONCE request in stream')
    check_invalid_watch_keys(cg, iproto_type.WATCH_ONCE)

    cg.server:eval('box.broadcast(...)', {'foo', {1, 2, 3}})
    body = request(cg, iproto_type.WATCH_ONCE, watch_body)
    t.assert_equals(body[key.DATA], {{1, 2, 3}})
end
