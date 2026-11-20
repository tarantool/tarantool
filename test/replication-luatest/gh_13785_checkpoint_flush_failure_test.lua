local t = require('luatest')
local server = require('luatest.server')

local g = t.group('gh_13785_checkpoint_flush_failure')

g.before_each(function(cg)
    cg.server = server:new({box_cfg = {replication_synchro_timeout = 120}})
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_pending_transaction_after_failed_fetch_snapshot = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        local socket = require('socket')
        local msgpack = require('msgpack')
        local uri = require('uri').parse(box.cfg.listen)
        local key = box.iproto.key

        -- Start sync transaction, so that limbo is not empty.
        box.ctl.promote()
        local s = box.schema.space.create('s', {is_sync = true})
        s:create_index('pk')
        box.cfg{replication_synchro_quorum = 2}
        local writer = fiber.new(function() s:insert{1} end)
        writer:set_joinable(true)
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
        end)
        box.ctl.wal_sync()
        -- Checkpoint creation will fail.
        box.error.injection.set('ERRINJ_WAL_SYNC', true)
        -- Trigger in memory checkpoint with FETCH_SNAPSHOT.
        local sock = socket.tcp_connect(uri.host, uri.service)
        t.assert_equals(#sock:read(box.iproto.GREETING_SIZE, 120),
                        box.iproto.GREETING_SIZE)
        sock:write(box.iproto.encode_packet({
            [key.REQUEST_TYPE] = box.iproto.type.FETCH_SNAPSHOT,
        }, {[key.INSTANCE_UUID] = tostring(require('uuid').new())}))
        -- Error, but do not crash.
        local size = sock:read(5, 120)
        local header = box.iproto.decode_packet(size ..
            sock:read(msgpack.decode(size), 120))
        t.assert_equals(header[key.REQUEST_TYPE],
                        0x8000 + box.error.INJECTION)

        -- Restore everything back.
        sock:close()
        box.error.injection.set('ERRINJ_WAL_SYNC', false)
        box.cfg{replication_synchro_quorum = 1}
        t.assert(writer:join())
        t.assert_equals(s:get{1}, {1})
    end)
end
