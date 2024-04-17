local t = require('luatest')
local cluster = require('luatest.replica_set')

--
-- gh-9094: when a replica subscribes, it might in the beginning try to position
-- its reader cursor to the end of a large xlog file. Positioning inside of this
-- file can take significant time during which the WAL reader yielded and tried
-- to send heartbeats, but couldn't, because the relay thread wasn't
-- communicating with the TX thread. And when there are no messages from TX for
-- too long time, the heartbeats to the replica are not being sent.
--
-- The relay must communicate with the TX thread even when subscribe is just
-- being started and opens a large xlog file.
--
local g = t.group('gh_9094')

g.before_all(function(cg)
    cg.cluster = cluster:new({})
    cg.master = cg.cluster:build_and_add_server{
        alias = 'master',
        box_cfg = {
            replication_timeout = 0.05,
        },
    }
    cg.replica = cg.cluster:build_and_add_server{
        alias = 'replica',
        box_cfg = {
            replication = {
                cg.master.net_box_uri,
            },
            replication_timeout = 0.05,
            replication_sync_timeout = 0,
            read_only = true,
        },
    }
    cg.cluster:start()
end)

g.after_all(function(cg)
    cg.cluster:drop()
end)

g.test_recovery = function(cg)
    t.tarantool.skip_if_not_debug()

    cg.master:exec(function()
        box.schema.space.create('test'):create_index('pk')
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:stop()
    -- Relay on subscribe will hang simulating a large xlog opening.
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_RELAY_WAL_START_DELAY', true)
        box.space.test:insert{1}
    end)
    cg.replica:start()
    -- If the bug would still exist, it would take relay x4 timeouts to notice
    -- the TX thread not "responding" and to stop sending heartbeats, and x4
    -- timeouts for the replica to fail due to not receiving heartbeats.
    cg.master:exec(function()
        require('fiber').sleep(box.cfg.replication_timeout * 8)
    end)
    -- If during the opening the relay was processing TX messages, the replica's
    -- upstream mustn't be in a failed state and master's TX thread must update
    -- the idle duration of the relay.
    cg.replica:exec(function()
        local upstream = box.info.replication[1].upstream
        t.helpers.retrying({}, function()
            upstream = box.info.replication[1].upstream
            t.assert_equals(upstream.status, 'sync')
        end)
        t.assert_equals(upstream.message, nil)
    end)
    cg.master:exec(function(replica_id)
        t.helpers.retrying({}, function()
            t.assert_le(box.info.replication[replica_id].downstream.idle,
                        box.cfg.replication_timeout)
        end)
        box.error.injection.set('ERRINJ_RELAY_WAL_START_DELAY', false)
    end, {cg.replica:get_instance_id()})
    cg.replica:wait_for_vclock_of(cg.master)
end
