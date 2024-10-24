local server = require('luatest.server')
local t = require('luatest')
local msgpack = require('msgpack')
local uri = require('uri')
local lsocket = require('socket')
local luuid = require('uuid')

local type = box.iproto.type
local key = box.iproto.key

local timeout = 10

local CANNOT_DELETE_EXPIRED_GC_CONSUMER_ERROR = "Cannot delete expired " ..
    "WAL GC consumers"
local EXPIRATION_MSG_PATTERN = "WAL GC consumer of anonymous replica %s " ..
    "has not been used for too long so it has been deleted"

local function escape_uuid(uuid)
    return string.gsub(uuid, '%-', '%%%-')
end

local function encode_map(map)
    return msgpack.object(setmetatable(map, {__serialize = 'map'}))
end

local function setmap(map)
    return setmetatable(map, {__serialize = 'map'})
end

local function socket_connect(server)
    local u = uri.parse(server.net_box_uri)
    local s = lsocket.tcp_connect(u.host, u.service)
    t.assert_not_equals(s, nil)
    -- Skip the greeting.
    s:read(box.iproto.GREETING_SIZE, timeout)
    return s
end

local function socket_write(s, header, body)
    return s:write(box.iproto.encode_packet(
        setmap(header), setmap(body)))
end

local function socket_read(s)
    local size_mp = s:read(5, timeout)
    t.assert_equals(#size_mp, 5)
    local size = msgpack.decode(size_mp)
    local response = s:read(size, timeout)
    t.assert_equals(#response, size)
    return box.iproto.decode_packet(size_mp .. response)
end

local function socket_restart(g)
    g.s:close()
    g.s = socket_connect(g.server)
end

-- Send IPROTO_OK request
local function write_ok(s, body)
    local header = {
        [key.REQUEST_TYPE] = box.iproto.type.OK,
        [key.SYNC] = 1,
    }
    if body == nil then
        body = {}
    end
    return socket_write(s, header, body)
end

-- Send IPROTO_FETCH_SNAPSHOT request
local function write_fetch_snapshot(s, uuid)
    local header = {
        [key.REQUEST_TYPE] = box.iproto.type.FETCH_SNAPSHOT,
        [key.SYNC] = 1,
    }
    local body = {
        [key.INSTANCE_UUID] = uuid,
    }
    return socket_write(s, header, body)
end

-- Send anonymous IPROTO_SUBSCRIBE request
local function write_subscribe(s, uuid, replicaset_uuid, vclock)
    local header = {
        [key.REQUEST_TYPE] = box.iproto.type.SUBSCRIBE,
        [key.SYNC] = 1,
    }
    local body = {
        [key.REPLICASET_UUID] = replicaset_uuid,
        [key.INSTANCE_UUID] = uuid,
        [key.VCLOCK] = vclock,
        [key.REPLICA_ANON] = true,
    }
    return socket_write(s, header, body)
end

-- Helpers to read snap
local function extract_data(g, b)
    local space_name = g.id_to_name[b[key.SPACE_ID]]
    local k, v
    if space_name ~= nil then
        k = b[key.TUPLE][1]
        v = b[key.TUPLE][2]
    end
    return k, v
end

local function parse_data_insert(g, h, b)
    t.assert_equals(h[key.SYNC], 1)
    if h[key.REQUEST_TYPE] ~= type.INSERT then
        return
    end

    local k, v = extract_data(g, b)
    if k ~= nil then
        t.assert_not_equals(v, nil)
    end
end

local function parse_data_stream(g, stop_cond)
    local h, b = socket_read(g.s)
    local request_type = h[key.REQUEST_TYPE]
    while request_type == type.INSERT or request_type == type.RAFT_PROMOTE do
        parse_data_insert(g, h, b)
        if stop_cond ~= nil and stop_cond(h, b) == true then
            break
        end
        h, b = socket_read(g.s)
        request_type = h[key.REQUEST_TYPE]
    end
    return h, b
end

local function parse_fetch_snapshot_response(g)
    -- Respond to the FETCH_SNAP request with vclock of checkpoint.
    local h, _ = socket_read(g.s)
    t.assert_equals(h[key.REQUEST_TYPE], type.OK)
    -- Data itself
    parse_data_stream(g)
end

local function basic_memtx_vinyl_setup(g)
    g.snap_vclock, g.id_to_name = g.server:exec(function()
        local memtx = box.schema.space.create('memtx')
        local vinyl_1 = box.schema.space.create('vinyl_1', {engine = 'vinyl'})
        local vinyl_2 = box.schema.space.create('vinyl_2', {engine = 'vinyl'})
        memtx:create_index('pk')
        vinyl_1:create_index('pk')
        vinyl_2:create_index('pk')

        for i = 3, 1, -1 do
            -- Data for initial join.
            memtx:insert{i, 'memtx'}
            vinyl_1:insert{i, 'vinyl_1'}
            vinyl_2:insert{i, 'vinyl_2'}
        end
        local snap_vclock = box.info.vclock
        box.snapshot()
        -- Bump vclock one more time to be sure, that we're getting files
        -- and not a readview.
        box.space.memtx:insert{4, 'memtx'}

        local id_to_name = {
            [memtx.id] = memtx.name,
            [vinyl_1.id] = vinyl_1.name,
            [vinyl_2.id] = vinyl_2.name,
        }
        return snap_vclock, id_to_name
    end)
    g.spaces = {}
end

local g = t.group()

g.before_each(function(g)
    g.server = server:new{box_cfg = {checkpoint_count = 1}}
    g.server:start()
    basic_memtx_vinyl_setup(g)
    g.s = socket_connect(g.server)
end)

g.after_each(function(g)
    g.s:close()
    g.server:stop()
end)

g.test_fetch_snapshot_no_uuid = function(g)
    write_fetch_snapshot(g.s)
    local vclock = g.server:get_vclock()
    vclock[0] = nil
    parse_fetch_snapshot_response(g, vclock)

    g.server:exec(function()
        t.assert_equals(box.info.gc().consumers, {})
        t.assert_equals(box.space._gc_consumers:select{}, {})
    end)
end

g.test_fetch_snapshot = function(g)
    local uuid = luuid.str()
    write_fetch_snapshot(g.s, uuid)
    local vclock = g.server:get_vclock()
    vclock[0] = nil
    parse_fetch_snapshot_response(g)

    g.server:exec(function()
        -- Check consumer
        t.assert_equals(#box.info.gc().consumers, 1)
        t.assert_equals(#box.space._gc_consumers:select{}, 1)
        local consumer = box.space._gc_consumers:select{}[1]

        local consumer_vclock = consumer.vclock
        consumer_vclock[0] = nil
        local vclock = box.info.vclock
        vclock[0] = nil

        t.assert_equals(vclock, consumer_vclock)
        t.assert_equals(consumer.opts, {type = 'replica'})
    end)
end

g.test_subscribe = function(g)
    local replicaset_uuid = g.server:eval("return box.info.replicaset.uuid")
    local uuid = require('uuid').str()
    write_subscribe(g.s, uuid, replicaset_uuid, g.snap_vclock, true)
    local h = socket_read(g.s)
    t.assert_equals(h[key.REQUEST_TYPE], type.OK)

    -- Check if subscribe updated consumer
    g.server:exec(function(vclock)
        local consumers = box.space._gc_consumers:select{}
        t.assert_equals(#consumers, 1)
        t.assert_equals(consumers[1].vclock[1], vclock[1])
    end, {g.snap_vclock})

    local h = socket_read(g.s)
    t.assert_equals(h[key.REQUEST_TYPE], type.INSERT)

    -- Check if relay advances consumers of anonymous replicas
    local new_vclock = g.server:exec(function()
        box.space.memtx:insert{100, 'new'}
        box.space.vinyl_1:insert{100, 'new'}
        box.space.vinyl_2:insert{100, 'new'}
        box.snapshot()
        local vclock = box.info.vclock
        for i = 1, 10 do
            box.space.memtx:insert{100 + i}
        end
        return vclock
    end)[1]

    local cnt = 0
    while cnt < 10 do
        h = socket_read(g.s)
        if h[key.REQUEST_TYPE] ~= type.OK then
            cnt = cnt + 1
            t.assert_equals(h[key.REQUEST_TYPE], type.INSERT)
        end
    end

    g.server:exec(function(vclock)
        -- Check if relay advances consumers of anonymous replicas
        t.helpers.retrying({}, function(vclock)
            local consumers = box.space._gc_consumers:select{}
            t.assert_equals(#consumers, 1)
            t.assert_equals(consumers[1].vclock[1], vclock[1])
        end, {vclock})
    end, {new_vclock})
end

-- Typical scenario: fetch_snapshot -> subscribe
g.test_fetch_snapshot_subscribe = function(g)
    local replicaset_uuid = g.server:eval("return box.info.replicaset.uuid")
    local uuid = require('uuid').str()
    write_fetch_snapshot(g.s, uuid)
    local server_vclock = g.server:get_vclock()
    server_vclock[0] = nil
    parse_fetch_snapshot_response(g)

    write_subscribe(g.s, uuid, replicaset_uuid, g.snap_vclock)
    local h = socket_read(g.s)
    t.assert_equals(h[key.REQUEST_TYPE], type.OK)

    local new_vclock = g.server:exec(function()
        box.space.memtx:insert{100, 'new'}
        box.space.vinyl_1:insert{100, 'new'}
        box.space.vinyl_2:insert{100, 'new'}
        box.snapshot()
        local vclock = box.info.vclock
        for i = 1, 10 do
            box.space.memtx:insert{100 + i}
        end
        return vclock
    end)[1]

    local cnt = 0
    while cnt < 10 do
        h = socket_read(g.s)
        if h[key.REQUEST_TYPE] ~= type.OK then
            cnt = cnt + 1
            t.assert_equals(h[key.REQUEST_TYPE], type.INSERT)
        end
    end

    g.server:exec(function(vclock)
        -- Check if relay advances consumers of anonymous replicas
        t.helpers.retrying({}, function(vclock)
            local consumers = box.space._gc_consumers:select{}
            t.assert_equals(#consumers, 1)
            t.assert_equals(consumers[1].vclock[1], vclock[1])
        end, {vclock})
    end, {new_vclock})
end

local g = t.group('Expiration of consumers')

g.before_each(function(g)
    -- Small replication timeout to touch gc consumers frequently
    local box_cfg = {checkpoint_count = 1, replication_timeout = 0.3}
    g.server = server:new{box_cfg = box_cfg}
    g.server:start()
    basic_memtx_vinyl_setup(g)
    g.s = socket_connect(g.server)
end)

g.after_each(function(g)
    g.s:close()
    g.server:stop()
end)

g.test_expiration_fetch_snapshot = function(g)
    g.server:exec(function()
        box.cfg{replication_anon_gc_timeout = 0.1}
    end)
    local uuid = require('uuid').str()
    write_fetch_snapshot(g.s, uuid)
    parse_fetch_snapshot_response(g)

    -- Wait while the consumer will be deleted
    g.server:exec(function()
        t.helpers.retrying({}, function()
            t.assert_equals(box.space._gc_consumers:select{}, {})
        end)
    end)

    -- Expiration is logged
    local msg = string.format(EXPIRATION_MSG_PATTERN, escape_uuid(uuid))
    t.assert(g.server:grep_log(msg))

    -- No errors were reported
    t.assert_not(g.server:grep_log(CANNOT_DELETE_EXPIRED_GC_CONSUMER_ERROR))
end

-- The case checks if the consumer is not expired when fetching snapshot
-- for too long.
g.test_expiration_long_fetch_snapshot = function(g)
    t.tarantool.skip_if_not_debug()
    local fiber = require('fiber')
    local uuid = require('uuid').str()
    g.server:exec(function() box.cfg{replication_anon_gc_timeout = 0.1} end)

    write_fetch_snapshot(g.s, uuid)
    local h = socket_read(g.s)
    t.assert_equals(h[key.REQUEST_TYPE], type.OK)

    g.server:exec(function()
        box.error.injection.set('ERRINJ_RELAY_TIMEOUT', 1)
    end)
    fiber.sleep(0.3)

    g.server:exec(function()
        -- Consumer is still registered
        t.assert_not_equals(box.space._gc_consumers:select{}, {})
        t.assert_not_equals(box.info.gc().consumers, {})
    end)

    g.server:exec(function()
        box.error.injection.set('ERRINJ_RELAY_TIMEOUT', 0)
    end)
    socket_restart(g)

    -- Wait while the consumer will be deleted
    g.server:exec(function()
        t.helpers.retrying({}, function()
            t.assert_equals(box.space._gc_consumers:select{}, {})
        end)
    end)
end

g.test_expiration_subscribe = function(g)
    local fiber = require('fiber')
    g.server:exec(function()
        box.cfg{replication_anon_gc_timeout = 0.1}
    end)
    local replicaset_uuid = g.server:eval("return box.info.replicaset.uuid")
    local uuid = require('uuid').str()
    write_subscribe(g.s, uuid, replicaset_uuid, g.snap_vclock)
    local h = socket_read(g.s)
    t.assert_equals(h[key.REQUEST_TYPE], type.OK)

    -- Sleep for a while and check if the consumer is still alive
    fiber.sleep(0.2)
    g.server:exec(function()
        t.assert_equals(#box.space._gc_consumers:select{}, 1)
        t.assert_equals(#box.info.gc().consumers, 1)
    end)

    socket_restart(g)

    g.server:exec(function()
        -- Wait while the consumer will be deleted
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.gc().consumers, {})
            t.assert_equals(box.space._gc_consumers:select{}, {})
        end)
    end)

    -- Expiration is logged
    local msg = string.format(EXPIRATION_MSG_PATTERN, escape_uuid(uuid))
    t.assert(g.server:grep_log(msg))

    -- No errors were reported
    t.assert_not(g.server:grep_log(CANNOT_DELETE_EXPIRED_GC_CONSUMER_ERROR))
end

-- The case checks if the consumer is not expired when the replica is subscribed
-- but receives nothing so its consumer is not advanced actually.
g.test_expiration_idle_subscribe = function(g)
    local fiber = require('fiber')
    local uuid = require('uuid').str()
    local replicaset_uuid = g.server:eval("return box.info.replicaset.uuid")
    g.server:exec(function() box.cfg{replication_anon_gc_timeout = 0.2} end)
    write_subscribe(g.s, uuid, replicaset_uuid, g.snap_vclock)
    local h = socket_read(g.s)
    t.assert_equals(h[key.REQUEST_TYPE], type.OK)

    -- Keep connected to master
    for _ = 1, 10 do
        fiber.sleep(0.1)
        write_ok(g.s, {[key.VCLOCK] = encode_map({})})
    end

    g.server:exec(function()
        -- Consumer is still registered
        t.assert_not_equals(box.info.gc().consumers, {})
        t.assert_not_equals(box.space._gc_consumers:select{}, {})
    end)

    socket_restart(g)

    -- Wait while the consumer will be deleted
    g.server:exec(function()
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.gc().consumers, {})
            t.assert_equals(box.space._gc_consumers:select{}, {})
        end)
    end)
end
