local t = require('luatest')
local uuid = require('uuid')
local server = require('luatest.server')
local proxy = require('luatest.replica_proxy')
local msgpack = require('msgpack')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.master = server:new{alias = 'master'}
    cg.master:start()
    cg.master:exec(function()
        box.schema.space.create('test'):create_index('pk')
        box.space.test:insert{1, 'data'}
        box.space.test:insert{2, 'data'}
    end)
end)

g.after_all(function(cg)
    cg.master:stop()
end)

g.test_applier_stops_on_error_during_fetch_snapshot = function(cg)
    local replica_uuid = uuid.str()
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
    end)
    local replica = server:new{
        alias = 'replica',
        box_cfg = { instance_uuid = replica_uuid,
                    replication = { cg.master.net_box_uri } },
    }

    replica:start({wait_until_ready = false})
    -- Block _cluster WAL write after snapshot is sent but before OK marker.
    local initial_cluster_count = cg.master:exec(function()
        return box.space._cluster:count()
    end)
    cg.master:exec(function(initial_count)
        t.helpers.retrying({timeout=10}, function(initial_count)
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
            local is_cluster_write = box.space._cluster:count() > initial_count
            if not is_cluster_write then
                box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
                box.error.injection.set('ERRINJ_WAL_DELAY', false)
            end
            t.assert(is_cluster_write)
        end, initial_count)
    end, {initial_cluster_count})

    t.helpers.retrying({timeout=10}, function()
        t.assert_not_equals(cg.master:grep_log('initial data sent'), nil)
    end)
    cg.master.process:kill("KILL");
    cg.master:restart()

    -- Applier should see that replica has some data and stop itself from
    -- reconnecting.
    t.helpers.retrying({timeout = 10}, function()
        t.assert_not(replica.process:is_alive())
        t.assert_not_equals(replica:grep_log('Error occurred during' ..
        ' fetching snapshot, but some data has been applied.' ..
        ' Stopping applier'), nil)
        t.assert_equals(replica:grep_log('will retry every'), nil)
    end)

    cg.master:exec(function(replica_uuid)
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', -1)
        box.ctl.replica_gc(replica_uuid)
    end, {replica_uuid})
    replica:drop()
end

g.test_applier_stops_on_error_during_fetch_snapshot_rebootstrap = function(cg)
    local replica_uuid = uuid.str()
    local replica = server:new{
        alias = 'replica',
        box_cfg = { instance_uuid = replica_uuid,
                    replication = { cg.master.net_box_uri } },
    }
    replica:start()
    replica:stop()

    cg.master:exec(function(replica_uuid)
        box.ctl.replica_gc(replica_uuid)
        box.cfg{wal_cleanup_delay = 0, checkpoint_count = 1}
        box.space.test:insert{3, 'data'}
        box.snapshot()
        box.error.injection.set('ERRINJ_REPLICA_JOIN_DELAY', true)
    end, {replica_uuid})

    replica:start({wait_until_ready = false})
    t.helpers.retrying({timeout=10}, function()
        t.assert_not_equals(replica:grep_log('initiating rebootstrap'), nil)
        t.assert_not_equals(cg.master:grep_log('initial data sent'), nil)
    end)
    cg.master.process:kill("KILL");
    cg.master:restart()

    t.helpers.retrying({timeout = 10}, function()
        t.assert_not(replica.process:is_alive())
        t.assert_not_equals(replica:grep_log('Error occurred during' ..
        ' fetching snapshot, but some data has been applied.' ..
        ' Stopping applier'), nil)
        t.assert_equals(replica:grep_log('will retry every'), nil)
    end)

    cg.master:exec(function()
        box.error.injection.set('ERRINJ_REPLICA_JOIN_DELAY', false)
    end)
    replica:drop()
end

--
-- The join metadata (the synchro queue and Raft states) is applied even before
-- the snapshot data. It changes the local state too, so a retry after it is not
-- safe either.
--
local g_meta = t.group('join_meta', {
    {replication_anon = false},
    {replication_anon = true},
})

g_meta.before_all(function(cg)
    cg.master = server:new({alias = 'master'})
    cg.master:start()
end)

g_meta.after_all(function(cg)
    cg.master:drop()
end)

g_meta.test_applier_stops_after_raft = function(cg)
    local interrupted = false
    local replica_proxy = proxy:new({
        client_socket_path = server.build_listen_uri('master_proxy'),
        server_socket_path = cg.master.net_box_uri,
        process_server = {
            -- Raft is the first metadata row. Cutting the stream here leaves
            -- the applier in WAIT_SNAPSHOT with exactly one applied row.
            pre = function(conn)
                if interrupted then
                    return
                end
                local sock = conn.server_socket
                conn:forward_to_client(sock:read(box.iproto.GREETING_SIZE, 120))
                while true do
                    local prefix = sock:read(5, 120)
                    if prefix == nil or #prefix == 0 then
                        return
                    end
                    local data = sock:read(msgpack.decode(prefix), 120)
                    local packet = prefix .. data
                    local header = box.iproto.decode_packet(packet)
                    conn:forward_to_client(packet)
                    if interrupted then
                        return
                    end
                    if header[box.iproto.key.REQUEST_TYPE] ==
                       box.iproto.type.RAFT then
                        interrupted = true
                        conn:stop()
                        return
                    end
                end
            end,
            func = function(conn, data) conn:forward_to_client(data) end,
            post = function(conn) conn:stop() end,
        },
    })
    t.assert(replica_proxy:start({force = true}))
    local replica = server:new({
        alias = 'replica',
        box_cfg = {
            replication = {replica_proxy.client_socket_path},
            replication_anon = cg.params.replication_anon,
            read_only = cg.params.replication_anon,
            replication_connect_timeout = 120,
            replication_reconnect_timeout = 0.01,
        },
    })
    replica:start({wait_until_ready = false})
    t.helpers.retrying({timeout = 120}, function()
        t.assert_not(replica.process:is_alive())
        -- Luatest may still be forwarding the process output to the log.
        t.assert_not_equals(replica:grep_log(
            'Error occurred during fetching snapshot, but some data has ' ..
            'been applied. Stopping applier'), nil)
        t.assert_not_equals(replica:grep_log("can't initialize storage:"), nil)
    end)
    t.assert(interrupted)
    t.assert_equals(replica:grep_log('will retry every'), nil)
    replica:drop()
    replica_proxy:stop()
end
