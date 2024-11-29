local server = require('luatest.server')
local t = require('luatest')
local msgpack = require('msgpack')
local uri = require('uri')
local lsocket = require('socket')
local luuid = require('uuid')

local type = box.iproto.type
local key = box.iproto.key

local timeout = 10

-- Hint that table must be encoded as MP_MAP
local function setmap(map)
    return setmetatable(map, {__serialize = 'map'})
end

--
-- Helpers for IPROTO protocol communication via raw socket
--

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

--
-- Helpers to make IPROTO requests via raw socket
--

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

-- Read response to IPROTO_FETCH_SNAPSHOT request
local function read_fetch_snapshot_response(g)
    -- The first row is IPROTO_OK
    local h, _ = socket_read(g.s)
    t.assert_equals(h[key.REQUEST_TYPE], type.OK)
    -- Data stream - all rows are IPROTO_INSERT
    local h, _ = socket_read(g.s)
    local request_type = h[key.REQUEST_TYPE]
    while request_type == type.INSERT do
        h, _ = socket_read(g.s)
        request_type = h[key.REQUEST_TYPE]
    end
    -- The last row is IPROTO_OK
    t.assert_equals(h[key.REQUEST_TYPE], type.OK)
end


-- Send anonymous IPROTO_SUBSCRIBE request
local function write_subscribe(s, uuid, replicaset_uuid, vclock)
    -- A hint that vclock must be encoded as MP_MAP
    if vclock ~= nil then
        vclock = setmap(vclock)
    end
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

local g = t.group('WAL GC of anonymous replicas')

g.before_each(function(g)
    g.server = server:new{box_cfg = {checkpoint_count = 1}}
    g.server:start()
    g.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
        box.space.test:insert{0, 0}
    end)
    g.s = socket_connect(g.server)
end)

g.after_each(function(g)
    g.s:close()
    g.server:drop()
    if g.replica ~= nil then
        g.replica:drop()
        g.replica = nil
    end
end)

g.test_fetch_snapshot_no_uuid = function(g)
    write_fetch_snapshot(g.s)
    read_fetch_snapshot_response(g)

    g.server:exec(function()
        t.assert_equals(box.info.gc().consumers, {})
        t.assert_equals(box.space._gc_consumers:select{}, {})
    end)
end

-- Helper that checks if IPROTO_FETCH_SNAPSHOT correctly updates WAL GC state
local function test_check_fetch_snapshot(g, uuid)
    write_fetch_snapshot(g.s, uuid)
    read_fetch_snapshot_response(g)

    g.server:exec(function()
        -- Check consumer
        t.assert_equals(#box.info.gc().consumers, 1)
        t.assert_equals(#box.space._gc_consumers:select{}, 1)
        local consumer = box.space._gc_consumers:select{}[1]

        -- We fetch actual read-view here, so the latest vclock must be set
        local consumer_vclock = consumer.vclock
        consumer_vclock[0] = nil
        local vclock = box.info.vclock
        vclock[0] = nil

        t.assert_equals(vclock, consumer_vclock)
        t.assert_equals(consumer.opts, {type = 'replica'})
    end)
end

-- Helper that checks if IPROTO_SUBSCRIBE correctly updates WAL GC state
local function test_check_subscribe(g, uuid)
    local replicaset_uuid = g.server:eval("return box.info.replicaset.uuid")
    local vclock = g.server:get_vclock()
    vclock[0] = nil
    write_subscribe(g.s, uuid, replicaset_uuid, vclock)
    local h = socket_read(g.s)
    t.assert_equals(h[key.REQUEST_TYPE], type.OK)

    -- Check if subscribe created or updated consumer with passed vclock
    g.server:exec(function(vclock)
        local consumers = box.space._gc_consumers:select{}
        t.assert_equals(#consumers, 1)
        t.assert_equals(consumers[1].vclock, vclock)
    end, {vclock})

    -- Write some data
    g.server:exec(function()
        for i = 1, 10 do
            box.space.test:insert{i, i}
        end
        box.snapshot()
        box.space.test:insert{11, 11}
    end)
    -- Fetch written data
    while true do
        local _, b = socket_read(g.s)
        if table.equals(b[key.TUPLE], {11, 11}) then
            -- We've read the last tuple - stop fetching
            break
        end
    end
    -- Check if relay advances consumers of anonymous replica
    g.server:exec(function()
        -- Since consumers updated when closing another xlog file,
        -- take vclock of the latest checkpoint
        local vclock = box.info.gc().checkpoints[1].vclock
        vclock[0] = nil
        t.helpers.retrying({}, function()
            local consumers = box.space._gc_consumers:select{}
            t.assert_equals(#consumers, 1)
            t.assert_equals(consumers[1].vclock, vclock)
        end)
    end)
end

g.test_fetch_snapshot = function(g)
    local uuid = luuid.str()
    test_check_fetch_snapshot(g, uuid)
end

g.test_subscribe = function(g)
    local uuid = luuid.str()
    test_check_subscribe(g, uuid)
end

-- Typical scenario: fetch_snapshot -> subscribe
g.test_fetch_snapshot_subscribe = function(g)
    local uuid = luuid.str()
    test_check_fetch_snapshot(g, uuid)
    test_check_subscribe(g, uuid)
end

-- Make sure that we cannot fetch snapshot on already connected replica,
-- WAL GC relies on it.
g.test_fetch_snapshot_on_connected_replica = function(g)
    local uuid = luuid.str()
    test_check_subscribe(g, uuid)

    local s = socket_connect(g.server)
    write_fetch_snapshot(s, uuid)
    local h, b = socket_read(s)
    t.assert_equals(h[key.REQUEST_TYPE], type.TYPE_ERROR + box.error.CFG)
    t.assert_equals(b[key.ERROR_24],
        "Incorrect value for option 'replication': duplicate connection " ..
        "with the same replica UUID")
    s:close()
end

-- The test checks that if an anonyomus replica fails to register,
-- its WAL GC state and the replica itself are not deleted.
g.test_registration_fail = function(g)
    t.tarantool.skip_if_not_debug()
    local box_cfg = table.deepcopy(g.server.box_cfg)
    box_cfg.replication = {g.server.net_box_uri}
    box_cfg.read_only = true
    box_cfg.replication_anon = true
    g.replica = server:new({box_cfg = box_cfg})
    g.replica:start()
    g.replica:wait_for_vclock_of(g.server)
    g.server:exec(function()
        box.error.injection.set('ERRINJ_RELAY_FINAL_JOIN', true)
    end)

    t.assert_error_msg_content_equals("Error injection 'relay final join'",
        g.replica.exec, g.replica,
        function() box.cfg{replication_anon = false} end)

    g.server:exec(function()
        t.assert_equals(box.info.replication_anon, {count = 1})
        t.assert_not_equals(box.info.gc().consumers, {})
        t.assert_not_equals(box.space._gc_consumers:select(), {})
    end)
end
