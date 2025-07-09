local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({
        box_cfg = {
            replication_synchro_queue_max_size = 1000,
            replication_synchro_timeout = 1000,
            election_mode = 'manual',
        }
    })
    cg.server:start()
    cg.server:exec(function()
        rawset(_G, 'fiber', require('fiber'))
        box.ctl.promote()
        box.ctl.wait_rw()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        rawset(_G, 'test_timeout', 60)
        rawset(_G, 'test_data', string.rep('a', 1000))
    end)
end)

g.after_each(function(cg)
    if cg.replica then
        cg.replica:drop()
        cg.replica = nil
    end
    cg.server:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
        for _, t in box.space._gc_consumers:pairs() do
            box.ctl.replica_gc(t.uuid)
        end
        t.assert_equals(box.info.replication_anon.count, 0)
        box.space.test:truncate()
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_cancel_before_waiting_for_limbo_space = function(cg)
    cg.server:exec(function()
        local s = box.space.test
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local f1 = _G.fiber.create(function()
            _G.fiber.self():set_joinable(true)
            s:insert{1, _G.test_data}
        end)
        local f2 = _G.fiber.create(function()
            local self = _G.fiber.self()
            self:set_joinable(true)
            pcall(self.cancel, self)
            -- Must fail right away, because the cancellation is checked before
            -- waiting for the limbo space.
            s:insert{2, _G.test_data}
        end)
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        local ok, err = f2:join()
        t.assert_not(ok)
        t.assert_covers(err:unpack(), {name = 'SYNC_ROLLBACK'})
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f1:join()))
        t.assert(box.space.test:get{1})
        t.assert_not(box.space.test:get{2})
        s:truncate()
    end)
end

--
-- gh-11180: joining a new replica needs to create a read-view with all the data
-- and ensure this read-view only has committed data. There was a bug that the
-- "waiting for commit of the read-view data" wouldn't notice a rollback of the
-- latest synchro txn if the latter was waiting for free space in the limbo at
-- the moment of read-view creation.
--
g.test_cascading_rollback_while_waiting_for_limbo_space = function(cg)
    --
    -- One txn is waiting inside WAL. Another one is waiting for limbo space.
    --
    cg.server:exec(function()
        local s = box.space.test

        rawset(_G, 'test_is_committed', false)
        local function make_txn_fiber(id)
            return _G.fiber.create(function()
                _G.fiber.self():set_joinable(true)
                box.begin()
                box.on_commit(function()
                    t.assert(not _G.test_is_committed)
                    _G.test_is_committed = true
                end)
                s:insert{id, _G.test_data}
                box.commit()
            end)
        end
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        rawset(_G, 'test_f1', make_txn_fiber(1))
        rawset(_G, 'test_f2', make_txn_fiber(2))
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
    end)
    --
    -- Replica tries to join and a read-view is created for it.
    --
    cg.replica = server:new({
        box_cfg = {
            replication = cg.server.net_box_uri,
            replication_timeout = 0.1,
            replication_anon = true,
            read_only = true,
        }
    })
    cg.replica:start({wait_until_ready = false})
    cg.server:exec(function()
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert_gt(box.info.replication_anon.count, 0)
        end)
        --
        -- The first txn is committed. The second one is stuck in WAL.
        --
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            if _G.test_is_committed then
                return
            end
            -- Allow one WAL entry at a time until txn1 is committed and txn2 is
            -- not.
            box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
            t.helpers.retrying({timeout = _G.test_timeout}, function()
                t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
            end)
            t.assert_not('retry')
        end)
        t.assert((_G.test_f1:join()))
        --
        -- Now txn2 fails to get written to WAL and is rolled back.
        --
        box.error.injection.set('ERRINJ_WAL_ROTATE', true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        local ok, err = _G.test_f2:join()
        t.assert_not(ok)
        t.assert_covers(err:unpack(), {name = 'WAL_IO'})
        box.error.injection.set('ERRINJ_WAL_ROTATE', false)

        t.assert(box.space.test:get{1})
        t.assert_not(box.space.test:get{2})
    end)
    --
    -- The replica's join process must fail due to the rollback, then it gets
    -- retried, and gets the correct data.
    --
    cg.replica:wait_until_ready()
    cg.replica:exec(function()
        t.assert(box.space.test:get{1})
        t.assert_not(box.space.test:get{2})
    end)
    --
    -- The replication continues working.
    --
    cg.server:exec(function()
        -- After WAL "error" the leader steps down.
        box.ctl.promote()
        box.ctl.wait_rw()
        box.space.test:insert{3}
    end)
    cg.replica:wait_for_vclock_of(cg.server)
    cg.replica:exec(function()
        t.assert(box.space.test:get{3})
    end)
    cg.server:exec(function()
        box.space.test:truncate()
    end)
end

--
-- gh-11180: the checkpoint creation would take the journal vclock of the
-- read-view ignoring the volatile limbo entries waiting for space in the limbo.
--
g.test_get_read_view_vclock_of_volatile_limbo_txns = function(cg)
    --
    -- One txn in WAL, second one waiting for space in the limbo.
    --
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local function make_txn_fiber(id)
            return _G.fiber.create(function()
                _G.fiber.self():set_joinable(true)
                box.space.test:insert{id, _G.test_data}
            end)
        end
        rawset(_G, 'test_f1', make_txn_fiber(1))
        rawset(_G, 'test_f2', make_txn_fiber(2))
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
    end)
    --
    -- The replica creates a new read-view and needs its vclock.
    --
    cg.replica = server:new({
        box_cfg = {
            replication = cg.server.net_box_uri,
            replication_timeout = 0.1,
            replication_anon = true,
            read_only = true,
        }
    })
    cg.replica:start({wait_until_ready = false})
    cg.server:exec(function()
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert_gt(box.info.replication_anon.count, 0)
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((_G.test_f1:join()))
        t.assert((_G.test_f2:join()))
        t.assert(box.space.test:get{1})
        t.assert(box.space.test:get{2})
    end)
    cg.replica:wait_until_ready()
    cg.replica:exec(function()
        t.assert(box.space.test:get{1})
        t.assert(box.space.test:get{2})
    end)
    --
    -- The replication still works.
    --
    cg.server:exec(function()
        box.space.test:insert{3}
    end)
    cg.replica:wait_for_vclock_of(cg.server)
    cg.replica:exec(function()
        t.assert(box.space.test:get{3})
    end)
end

--
-- gh-11180: during checkpoint creation the master has to make sure all the
-- txns in it are confirmed. And only then it can create a checkpoint of the
-- synchronous replication states like Raft and limbo.
--
-- There was a bug that the master, during waiting for the read-view txns to
-- get persisted, would miss a new synchro txn appearing, and would later wait
-- for its confirmation. Even though it is not even in the read-view. Then it
-- would send to the replica a too new synchro state
-- (confirm lsn > read-view's). Later the replica would receive the confirm of
-- the newer txn and would think it is a conflicting confirmation. Because the
-- replica would already have the too new confirmation lsn remembered.
--
g.test_get_limbo_checkpoint_exactly_on_time = function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local function make_txn_fiber(id, on_commit)
            return _G.fiber.create(function()
                _G.fiber.self():set_joinable(true)
                box.begin()
                box.on_commit(function()
                    if on_commit then
                        on_commit()
                    end
                end)
                box.space.test:insert{id, _G.test_data}
                box.commit()
            end)
        end
        rawset(_G, 'test_f1', make_txn_fiber(1))
        --
        -- Right when the first txn gets committed, the second one is created.
        -- It goes to the limbo and is trying to trick the master to wait for
        -- its confirmation instead of the first txn.
        --
        rawset(_G, 'test_f2', make_txn_fiber(2, function()
            rawset(_G, 'test_f3', make_txn_fiber(3))
        end))
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
    end)
    cg.replica = server:new({
        box_cfg = {
            replication = cg.server.net_box_uri,
            replication_timeout = 0.1,
            replication_anon = true,
            read_only = true,
        }
    })
    cg.replica:start({wait_until_ready = false})
    cg.server:exec(function()
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert_gt(box.info.replication_anon.count, 0)
        end)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((_G.test_f1:join()))
        t.assert((_G.test_f2:join()))
        t.assert((_G.test_f3:join()))
        t.assert(box.space.test:get{1})
        t.assert(box.space.test:get{2})
        t.assert(box.space.test:get{3})
    end)
    cg.replica:wait_until_ready()
    -- The third txn wasn't in the read-view. So it is going to be sent as a
    -- follow-up. Need to wait for it explicitly.
    cg.replica:wait_for_vclock_of(cg.server)
    cg.replica:exec(function()
        t.assert(box.space.test:get{1})
        t.assert(box.space.test:get{2})
        t.assert(box.space.test:get{3})
    end)
    --
    -- Ensure the replication still works.
    --
    cg.server:exec(function()
        box.space.test:insert{4}
    end)
    cg.replica:wait_for_vclock_of(cg.server)
    cg.replica:exec(function()
        t.assert(box.space.test:get{4})
    end)
end
