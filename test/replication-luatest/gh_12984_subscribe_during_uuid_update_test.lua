local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, t.helpers.matrix({
    memtx_use_mvcc_engine = {false, true},
}))

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({box_cfg = {
        memtx_use_mvcc_engine = cg.params.memtx_use_mvcc_engine,
    }})
    cg.server:start()
    cg.server:exec(function(listen)
        local msgpack = require('msgpack')
        local socket = require('socket')
        local uri = require('uri').parse(listen)
        local key = box.iproto.key
        local iproto_type = box.iproto.type

        rawset(_G, 'subscribe', function(instance_uuid)
            local s = socket.tcp_connect(uri.host, uri.service)
            t.assert(s)
            local greeting = s:read(box.iproto.GREETING_SIZE, 5)
            t.assert_equals(#greeting, box.iproto.GREETING_SIZE)
            local vclock = box.info.vclock
            vclock[0] = nil
            setmetatable(vclock, {__serialize = 'map'})
            t.assert(s:write(box.iproto.encode_packet({
                [key.REQUEST_TYPE] = iproto_type.SUBSCRIBE,
                [key.SYNC] = 1,
            }, {
                [key.REPLICASET_UUID] = box.info.replicaset.uuid,
                [key.INSTANCE_UUID] = instance_uuid,
                [key.INSTANCE_NAME] = 'replica',
                [key.VCLOCK] = vclock,
            })))
            return s
        end)

        rawset(_G, 'read_type', function(s)
            local size = s:read(5, 5)
            t.assert_equals(#size, 5)
            local body = s:read(msgpack.decode(size), 5)
            local header, decoded_body = box.iproto.decode_packet(size .. body)
            return header[key.REQUEST_TYPE], decoded_body
        end)
    end, {cg.server.net_box_uri})
end)

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:exec(function()
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
        end)
        cg.server:drop()
    end
end)

local function test_subscribe_during_change(cg, delete)
    cg.server:exec(function(delete)
        local fiber = require('fiber')
        local iproto_type = box.iproto.type
        local uuid = require('uuid')
        local old_uuid, new_uuid = uuid.str(), uuid.str()
        box.space._cluster:insert{2, old_uuid, 'replica'}

        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local writer = fiber.create(function()
            fiber.self():set_joinable(true)
            if delete then
                return pcall(box.space._cluster.delete, box.space._cluster, 2)
            end
            return pcall(box.space._cluster.update, box.space._cluster,
                         2, {{'=', 2, new_uuid}})
        end)
        t.helpers.retrying({}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        local s = _G.subscribe(old_uuid)
        t.helpers.retrying({}, function()
            t.assert(box.space._gc_consumers:get{old_uuid})
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local joined, ok, err = writer:join()
        t.assert(joined)
        t.assert(ok, tostring(err))
        t.assert_equals(_G.read_type(s), iproto_type.OK)
        local key = box.iproto.key
        local deadline = fiber.clock() + 5
        while true do
            t.assert_lt(fiber.clock(), deadline)
            local request_type, body = _G.read_type(s)
            t.assert_lt(request_type, iproto_type.TYPE_ERROR)
            if body[key.SPACE_ID] == box.space._cluster.id then
                t.assert_equals(request_type,
                                delete and iproto_type.DELETE or
                                           iproto_type.UPDATE)
                t.assert_equals(body[key.KEY], {2})
                break
            end
        end
        s:close()

        if delete then
            t.assert_equals(box.space._cluster:get{2}, nil)
            t.assert_equals(box.info.replication[2], nil)
        else
            t.assert_equals(box.space._cluster:get{2}[2], new_uuid)
            t.assert_equals(box.info.replication[2].uuid, new_uuid)
        end
    end, {delete})
end

g.test_uuid_update = function(cg)
    test_subscribe_during_change(cg, false)
end

g.test_delete = function(cg)
    test_subscribe_during_change(cg, true)
end
