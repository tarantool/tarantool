local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group('applier_stale_ack_on_reconnect')
local master_timeout = 0.2
local wait_timeout = 90

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new({})
    cg.master = cg.replica_set:build_and_add_server({
        alias = 'master',
        box_cfg = {
            -- Drops the connection on no ACK after 4 * replication_timeout.
            replication_timeout = master_timeout,
        },
    })
    cg.replica = cg.replica_set:build_and_add_server({
        alias = 'replica',
        box_cfg = {
            read_only = true,
            -- Larger timeout => longer reconnect sleep on the replica, giving
            -- the test a window to commit the pending rows and disable the
            -- writer delay before the second SUBSCRIBE.
            replication_reconnect_timeout = 3.0,
            replication = server.build_listen_uri('master', cg.replica_set.id),
        },
    })
    cg.replica_set:start()
    cg.master:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
    end)
    cg.replica:wait_for_vclock_of(cg.master)
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

--
-- gh-12795: the applier was able to send stale acks after reconnect.
-- The applier->thread struct is reused on every reconnect. The buggy
-- applier_thread_attach_applier() left has_acks_to_send/next_ack/txn_last_tm
-- from the previous connection in place, so the freshly started writer fiber
-- sent a stale (lower) ACK on the new connection. That ACK went backwards
-- relative to the new SUBSCRIBE vclock and tripped the monotonic-ACK assert in
-- the relay on the master (relay.cc, relay_process_ack()).
--
g.test_stale_ack_on_reconnect = function(cg)
    local master_id = cg.master:get_instance_id()
    local replica_id = cg.replica:get_instance_id()

    local function replica_lsn()
        return cg.replica:exec(function(id)
            return box.info.vclock[id] or 0
        end, {master_id})
    end

    -- Stop the master's heartbeats and wait until they have actually ceased
    -- (upstream goes idle), so none can overwrite the leftover ACK's tm later.
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_RELAY_REPORT_INTERVAL', 1000)
    end)
    cg.replica:exec(function(wait_timeout, master_timeout, id)
        t.helpers.retrying({timeout = wait_timeout}, function()
            local info = box.info.replication[id]
            local idle = info and info.upstream and info.upstream.idle or 0
            t.assert_gt(idle, 2 * master_timeout)
        end)
    end, {wait_timeout, master_timeout, master_id})

    -- Pause the replica's writer. From now on the replica sends no ACKs, so
    -- the master will eventually drop the connection.
    cg.replica:exec(function()
        box.error.injection.set('ERRINJ_APPLIER_WRITER_DELAY', true)
    end)

    -- Commit exactly one row. It produces a real-transaction ACK that the
    -- paused writer cannot consume, so has_acks_to_send stays true with
    -- next_ack == v_old and a non-zero tm. No heartbeat follows, so the tm is
    -- preserved.
    local v_start = replica_lsn()
    cg.master:exec(function()
        box.space.test:replace{1}
    end)
    t.helpers.retrying({timeout = wait_timeout}, function()
        t.assert_ge(replica_lsn(), v_start + 1)
    end)
    local v_old = replica_lsn()

    -- Hold the next batch pending in the replica's WAL while the master
    -- advances to v_new. The replica receives and submits these rows but does
    -- not commit them, so its vclock stays at v_old and no new ACK fires.
    cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
    end)
    cg.master:exec(function()
        for _ = 1, 10 do
            box.space.test:replace{1}
        end
    end)

    -- Pause the master relay so it doesn't read the second connection's first
    -- (stale) ACK. This guarantees status_msg.vclock is updated to the new
    -- high subscribe vclock before the stale ACK is processed.
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_RELAY_READ_ACK_DELAY', true)
    end)

    -- Wait for the master to drop the connection due to ACK starvation.
    -- detach() then runs on the replica: has_acks_to_send/next_ack/txn_last_tm
    -- are retained, the applier's on_wal_write trigger is cleared, and
    -- conn-1's writer exits (it returns on cancellation without clearing
    -- has_acks_to_send).
    t.helpers.retrying({timeout = wait_timeout}, function()
        local status = cg.replica:exec(function(id)
            local info = box.info.replication[id]
            return info and info.upstream and info.upstream.status
        end, {master_id})
        t.assert_not_equals(status, 'follow')
    end)

    -- Inside the reconnect window, before the applier reattaches: commit the
    -- pending rows (the applier's trigger is gone, so the replica's vclock
    -- reaches v_new while next_ack stays v_old), then unmask the bug by
    -- clearing the writer delay so the second connection's writer emits the
    -- stale ACK.
    cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    t.helpers.retrying({timeout = wait_timeout}, function()
        t.assert_gt(replica_lsn(), v_old)
    end)
    cg.replica:exec(function()
        box.error.injection.set('ERRINJ_APPLIER_WRITER_DELAY', false)
    end)
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_RELAY_REPORT_INTERVAL', 0)
    end)

    -- Wait until the second SUBSCRIBE reached the master relay (downstream
    -- vclock present => status updated), then let the relay read the stale
    -- ACK. On buggy code the relay aborts here; with the fix the stale ACK is
    -- never sent.
    cg.master:exec(function(id, timeout)
        local fiber = require('fiber')
        t.helpers.retrying({timeout = timeout}, function()
            local info = box.info.replication[id]
            if info and info.downstream and info.downstream.vclock and
               next(info.downstream.vclock) then
                return
            end
            error('No second subscribe from the replica')
        end)
        -- Let the relay <-> TX status exchange happen so status_msg.vclock is
        -- the new (high) subscribe vclock.
        t.helpers.retrying({timeout = timeout}, function()
            local ack_period = box.cfg.replication_timeout
            fiber.sleep(ack_period)
            local info = box.info.replication[id]
            if info and info.downstream and info.downstream.idle and
               info.downstream.idle < ack_period then
                return
            end
            error('No report to TX thread')
        end)
        box.error.injection.set('ERRINJ_RELAY_READ_ACK_DELAY', false)
    end, {replica_id, wait_timeout})

    -- With the fix the master stays alive and replication converges.
    cg.replica:wait_for_vclock_of(cg.master)
end
