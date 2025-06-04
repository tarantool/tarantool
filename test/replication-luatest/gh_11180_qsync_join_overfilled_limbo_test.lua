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

g.after_all(function(cg)
    cg.server:drop()
    if cg.replica then
        cg.replica:drop()
    end
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
