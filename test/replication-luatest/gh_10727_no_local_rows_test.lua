local t = require('luatest')
local server = require('luatest.server')
local msgpack = require('msgpack')
local socket = require('socket')
local uuid = require('uuid')
local uri = require('uri')

local g = t.group()

local timeout = 60
local key = box.iproto.key
local type = box.iproto.type

local function socket_connect(server)
    local u = uri.parse(server.net_box_uri)
    local s = socket.tcp_connect(u.host, u.service)
    t.assert_not_equals(s, nil)
    -- Skip the greeting.
    s:read(box.iproto.GREETING_SIZE, timeout)
    return s
end

local function socket_write(s, header, body)
    return s:write(box.iproto.encode_packet(header, body))
end

local function socket_read(s)
    local size_mp = s:read(5, timeout)
    t.assert_equals(#size_mp, 5)
    local size = msgpack.decode(size_mp)
    local response = s:read(size, timeout)
    t.assert_equals(#response, size)
    return box.iproto.decode_packet(size_mp .. response)
end

local function write_join(s, version_id, instance_uuid)
    local header = {
        [key.REQUEST_TYPE] = box.iproto.type.JOIN,
        [key.SYNC] = 1,
    }
    local body = {
        -- Trigger meta stage.
        [key.SERVER_VERSION] = version_id,
        [key.INSTANCE_UUID] = instance_uuid,
    }
    return socket_write(s, header, body)
end

local function encode_map(map)
    return msgpack.object(setmetatable(map, {__serialize = 'map'}))
end

local function write_subscribe(s, version_id, instance_uuid, rs_uuid, vclock)
    local header = {
        [key.REQUEST_TYPE] = box.iproto.type.SUBSCRIBE,
        [key.SYNC] = version_id,
    }
    local body = {
        [key.REPLICASET_UUID] = rs_uuid,
        [key.INSTANCE_UUID] = instance_uuid,
        [key.VCLOCK] = encode_map(vclock),
    }
    return socket_write(s, header, body)
end

local function parse_replication_stream(s, assert, cond)
    local h, b
    while true do
        h, b = socket_read(s)
        assert(h)
        if cond(h, b) then
            break
        end
    end
    return b
end

local function until_vclock_cond(h, b)
    return h[key.REQUEST_TYPE] == type.OK and b and b[key.VCLOCK]
end

local function until_promote_cond(h, _)
    return h[key.REQUEST_TYPE] == type.RAFT_PROMOTE
end

local function test_replication_stream(cg, version_id, assert)
    local s = socket_connect(cg.master)
    --
    -- JOIN stage.
    --
    local instance_uuid = uuid.str()
    write_join(s, version_id, instance_uuid)
    -- Start of initial join.
    parse_replication_stream(s, assert, until_vclock_cond)
    -- End of initial join.
    parse_replication_stream(s, assert, until_vclock_cond)
    -- End of final join.
    local b = parse_replication_stream(s, assert, until_vclock_cond)
    local expected_vclock = cg.master:get_vclock()
    expected_vclock[0] = nil
    t.assert_equals(b[key.VCLOCK], expected_vclock)
    --
    -- SUBSCRIBE stage.
    --
    local rs_uuid = cg.master:eval('return box.info.replicaset.uuid')
    write_subscribe(s, version_id, instance_uuid, rs_uuid, b[key.VCLOCK])
    cg.master:eval('pcall(box.ctl.promote)')
    parse_replication_stream(s, assert, until_promote_cond)
end

g.before_each(function(cg)
    cg.master = server:new{alias = 'master'}
    cg.master:start()
    cg.master:exec(function()
        box.schema.space.create('test', {is_local = true}):create_index('pk')
        box.space.test:insert{1, 'data'}
    end)
end)

g.after_each(function(cg)
    cg.master:stop()
end)

local no_local_rows_versions = {
   ['3.2.2'] = 197122,
   ['2.11.6'] = 133894,
}

for name, v in ipairs(no_local_rows_versions) do
    g['test_no_local_rows_are_sent_' .. name] = function(cg)
        test_replication_stream(cg, v, function(h)
            t.assert_not_equals(h[key.GROUP_ID], 1)
        end)
    end
end

local local_raft_versions = {
    ['3.2.1'] = 197121,
    ['2.11.5'] = 133893,
}

for name, v in ipairs(local_raft_versions) do
    g['test_local_rows_are_sent_' .. name] = function(cg)
        test_replication_stream(cg, v, function(h)
            if h[key.REQUEST_TYPE] == type.RAFT then
                t.assert_equals(h[key.GROUP_ID], 1)
            else
                t.assert_not_equals(h[key.GROUP_ID], 1)
            end
        end)
    end
end
