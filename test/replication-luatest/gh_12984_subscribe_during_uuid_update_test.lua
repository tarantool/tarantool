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
    cg.server:exec(function(listen, delete)
        local fiber = require('fiber')
        local msgpack = require('msgpack')
        local socket = require('socket')
        local uri = require('uri').parse(listen)
        local uuid = require('uuid')
        local key = box.iproto.key
        local iproto_type = box.iproto.type
        local old_uuid, new_uuid = uuid.str(), uuid.str()
        box.space._cluster:insert{2, old_uuid, 'replica'}

        local function subscribe(instance_uuid)
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
        end

        local function read_type(s)
            local size = s:read(5, 5)
            t.assert_equals(#size, 5)
            local body = s:read(msgpack.decode(size), 5)
            local header = box.iproto.decode_packet(size .. body)
            return header[key.REQUEST_TYPE]
        end

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
        local s = subscribe(old_uuid)
        -- Wait for either an early rejection or the GC consumer's WAL write.
        t.helpers.retrying({}, function()
            t.assert(s:readable(0) or box.space._gc_consumers:get{old_uuid})
        end)
        local waiting_for_wal = box.space._gc_consumers:get{old_uuid} ~= nil
        t.assert_equals(waiting_for_wal, delete)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local joined, ok, err = writer:join()
        t.assert(joined)
        t.assert(ok, tostring(err))
        local response_type = read_type(s)
        if response_type == iproto_type.OK then
            response_type = read_type(s)
        end
        t.assert_equals(response_type, iproto_type.TYPE_ERROR +
                                       box.error.TOO_EARLY_SUBSCRIBE)
        if not delete then
            t.assert_equals(box.space._gc_consumers:get{old_uuid}, nil)
        end
        s:close()

        if delete then
            t.assert_equals(box.space._cluster:get{2}, nil)
            t.assert_equals(box.info.replication[2], nil)
        else
            t.assert_equals(box.space._cluster:get{2}[2], new_uuid)
            t.assert_equals(box.info.replication[2].uuid, new_uuid)
        end
    end, {cg.server.net_box_uri, delete})
end

g.test_uuid_update = function(cg)
    test_subscribe_during_change(cg, false)
end

g.test_delete = function(cg)
    test_subscribe_during_change(cg, true)
end

g.test_subscribe_acks_delete = function(cg)
    cg.server:exec(function(listen)
        local fiber = require('fiber')
        local msgpack = require('msgpack')
        local socket = require('socket')
        local uri = require('uri').parse(listen)
        local key = box.iproto.key
        local iproto_type = box.iproto.type
        local uuid = require('uuid').str()
        box.ctl.promote()
        box.space._cluster:insert{2, uuid, 'replica'}
        box.space._cluster:alter{is_sync = true}
        box.cfg{replication_synchro_quorum = 2,
                replication_synchro_timeout = 10}
        local lsn = box.info.vclock[box.info.id]
        local writer = fiber.create(function()
            fiber.self():set_joinable(true)
            return pcall(box.space._cluster.delete, box.space._cluster, 2)
        end)
        t.helpers.retrying({}, function()
            t.assert_gt(box.info.vclock[box.info.id], lsn)
        end)
        t.assert_equals(box.info.synchro.queue.len, 1)
        t.assert_equals(box.space._cluster:get{2}, nil)
        t.assert_equals(box.info.replication[2].uuid, uuid)

        local s = socket.tcp_connect(uri.host, uri.service)
        t.assert(s)
        t.assert_equals(#s:read(box.iproto.GREETING_SIZE, 5),
                        box.iproto.GREETING_SIZE)
        local vclock = box.info.vclock
        vclock[0] = nil
        setmetatable(vclock, {__serialize = 'map'})
        t.assert(s:write(box.iproto.encode_packet({
            [key.REQUEST_TYPE] = iproto_type.SUBSCRIBE,
            [key.SYNC] = 1,
        }, {
            [key.REPLICASET_UUID] = box.info.replicaset.uuid,
            [key.INSTANCE_UUID] = uuid,
            [key.INSTANCE_NAME] = 'replica',
            [key.VCLOCK] = vclock,
        })))
        local size = s:read(5, 5)
        t.assert_equals(#size, 5)
        local body = s:read(msgpack.decode(size), 5)
        local header = box.iproto.decode_packet(size .. body)
        t.assert_equals(header[key.REQUEST_TYPE], iproto_type.OK)
        t.assert(s:write(box.iproto.encode_packet({
            [key.REQUEST_TYPE] = iproto_type.OK,
            [key.SYNC] = 1,
        }, {[key.VCLOCK] = vclock})))
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.replication[2], nil)
        end)
        local joined, ok, err = writer:join()
        t.assert(joined)
        t.assert(ok, tostring(err))
        s:close()
    end, {cg.server.net_box_uri})
end
