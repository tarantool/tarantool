local server = require('luatest.server')
local t = require('luatest')
local uuid = require('uuid')

local g = t.group('anon-relay-last-row-time')

local wait_timeout = 120

--
-- The snapshot is sent to the replica row by row: `xrow_stream_flush_size` is
-- set to 1 so that every row is flushed separately, and `ERRINJ_RELAY_TIMEOUT`
-- makes the relay sleep `row_send_delay` seconds after every flushed row. So
-- the fetch_snapshot part of the bootstrap lasts for about
-- `row_count * row_send_delay` seconds, which is enough to observe the relay
-- while it is busy sending the snapshot.
--
local row_count = 10
local row_send_delay = 0.01

--
-- Return the moment when the master's relay to the given anonymous replica
-- sent a row for the last time, or nil if there is no such relay. box.info
-- exposes only the time elapsed since that moment (`downstream.idle`), so the
-- monotonic clock is used to restore an absolute value which is comparable
-- between calls. Both values are read in the master's TX thread.
--
local function relay_last_row_time(replica_uuid)
    local anon = box.info.replication_anon()[replica_uuid]
    if anon == nil or anon.downstream == nil or
       anon.downstream.idle == nil then
        return nil
    end
    return require('fiber').clock() - anon.downstream.idle
end

g.before_each(function(cg)
    cg.master = server:new({
        alias = 'master',
        box_cfg = { replication_timeout = 0.1 },
    })
    cg.master:start()
    cg.master:exec(function(count)
        local s = box.schema.space.create('test')
        s:create_index('pk')
        for i = 1, count do
            s:replace({i, i})
        end
    end, {row_count})
end)

g.after_each(function(cg)
    if cg.replica ~= nil then
        cg.replica:drop()
        cg.replica = nil
    end
    cg.master:exec(function()
        local tweaks = require('internal.tweaks')
        local flush_size = rawget(_G, 'test_xrow_stream_flush_size')
        if flush_size ~= nil then
            tweaks.xrow_stream_flush_size = flush_size
            rawset(_G, 'test_xrow_stream_flush_size', nil)
        end
        box.error.injection.set('ERRINJ_RELAY_TIMEOUT', 0)
    end)
    cg.master:drop()
end)

--
-- The relay of the master must update its last_row_time while it sends a
-- snapshot to an anonymous replica. Otherwise a replica which fetches a big
-- snapshot for a long time looks idle to the master and may be removed by the
-- anonymous replica GC right in the middle of the bootstrap.
--
g.test_last_row_time_is_updated_during_fetch_snapshot = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.master:exec(function(delay)
        local tweaks = require('internal.tweaks')
        -- Flush every snapshot row separately, so that the delay below is
        -- applied to each row sent by the relay.
        rawset(_G, 'test_xrow_stream_flush_size',
               tweaks.xrow_stream_flush_size)
        tweaks.xrow_stream_flush_size = 1
        -- Make the snapshot sending long.
        box.error.injection.set('ERRINJ_RELAY_TIMEOUT', delay)
    end, {row_send_delay})

    -- The anonymous replica is not bootstrapped at the beginning: it is
    -- started without waiting for it to be ready, so that the master's relay
    -- can be observed while the snapshot is being fetched.
    local replica_uuid = uuid.str()
    cg.replica = server:new({
        alias = 'replica',
        box_cfg = {
            instance_uuid = replica_uuid,
            replication_anon = true,
            read_only = true,
            replication = {cg.master.net_box_uri},
            replication_timeout = 0.1,
        },
    })
    cg.replica:start({wait_until_ready = false})

    -- Wait until the master starts sending the snapshot. From this moment on
    -- the relay is busy sending rows, while the replica has not received the
    -- snapshot yet.
    t.helpers.retrying({timeout = wait_timeout}, function()
        t.assert(cg.master:grep_log('sending read%-view to replica'))
    end)

    -- relay->last_row_time before the replica receives the snapshot.
    local time_before
    t.helpers.retrying({timeout = wait_timeout}, function()
        time_before = cg.master:exec(relay_last_row_time, {replica_uuid})
        t.assert_not_equals(time_before, nil, 'relay to the anonymous ' ..
                            'replica is not running during fetch_snapshot')
    end)
    -- The snapshot is still on the way, so the value above was taken during
    -- fetch_snapshot and not after it.
    t.assert_not(cg.master:grep_log('read%-view sent'),
                 'the snapshot is already sent, cannot observe ' ..
                 'relay->last_row_time during fetch_snapshot')

    -- The relay is sending the rest of the snapshot right now, so its
    -- last_row_time must be advanced before the snapshot is over. Wait for
    -- the snapshot to be finished to compare the value taken before it with
    -- the one taken after it.
    local time_after
    t.helpers.retrying({timeout = wait_timeout}, function()
        time_after = cg.master:exec(relay_last_row_time, {replica_uuid})
        t.assert_not_equals(time_after, nil, 'relay to the anonymous ' ..
                            'replica is not running during fetch_snapshot')
        if cg.master:grep_log('read%-view sent') ~= nil then
            return
        end
        t.assert_gt(time_after, time_before, 'relay->last_row_time is not ' ..
                    'updated while the snapshot is being sent')
        error('the snapshot is not sent yet')
    end)
    t.assert_gt(time_after, time_before,
                'relay->last_row_time was not updated during fetch_snapshot')

    -- The replica finishes the bootstrap: it applies the snapshot, creates
    -- the initial checkpoint and subscribes to the master.
    cg.replica:wait_until_ready()
    cg.replica:exec(function(count)
        t.assert_equals(box.info.id, 0)
        t.assert_equals(box.space.test:count(), count)
    end, {row_count})
end
