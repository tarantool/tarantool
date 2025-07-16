local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

local test_timeout = 60

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({
        box_cfg = {
            replication_synchro_timeout = 1000,
            election_mode = 'manual',
        }
    })
    cg.server:start()
    cg.server:exec(function(test_timeout)
        rawset(_G, 'fiber', require('fiber'))
        box.ctl.promote()
        box.ctl.wait_rw()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        rawset(_G, 'test_timeout', test_timeout)
        rawset(_G, 'test_data', string.rep('a', 1000))
    end, {test_timeout})
end)

g.after_each(function(cg)
    if cg.replica then
        cg.replica:drop()
        cg.replica = nil
    end
    cg.server:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
        t.assert_equals(box.info.replication_anon.count, 0)
        box.space.test:truncate()
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- gh-11790 and gh-11180: during checkpoint creation the master has to make sure
-- all the txns in it are confirmed. And only then it can create a checkpoint of
-- the synchronous replication states like Raft and limbo.
--
-- There was a bug that the master, during waiting for the read-view txns to
-- get persisted and the journal synced, would miss a new synchro txn appearing,
-- and would later wait for its confirmation. Even though it is not even in the
-- read-view. Then it would send to the replica a too new synchro state
-- (confirm lsn > read-view's). Later the replica would receive the confirm of
-- the newer txn and would think it is a conflicting confirmation. Because the
-- replica would already have the too new confirmation lsn remembered.
--
-- In gh-11180 this bug would be reproducible and fixable on its own, because
-- some txns could have been blocked on the limbo's max size being reached. But
-- for gh-11790 on 3.2 this test is mostly doing the same as the next ones in
-- this file. It is cherry-picked from 3.6 for consistency.
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
        -- On Tarantool 3.3 and newer right when the first txn gets committed,
        -- the second one is created. It goes to the limbo and is trying to
        -- trick the master to wait for its confirmation instead of the first
        -- txn.
        --
        -- On 3.2 and older all the txns right after creation are just sent to
        -- WAL directly without waiting for free space in the limbo.
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
    t.helpers.retrying({timeout = test_timeout}, function()
        t.assert(cg.server:grep_log('sending read%-view'))
    end)
    cg.server:exec(function()
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

--
-- Collection of the limbo checkpoint must happen exactly when the last synchro
-- txn included into the checkpoint gets committed. Moreover, this checkpoint
-- must not cover any newer txns. Or the replica would mistakenly believe it
-- has confirmed even the data that wasn't sent to it.
--
g.test_limbo_checkpoint_batch_confirm = function(cg)
    --
    -- The test is making the limbo write a single CONFIRM for 2 txns: one
    -- included into the checkpoint for a new replica and one is not. The
    -- checkpoint sent to the replica must not include the last txn.
    --
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        rawset(_G, 'make_txn_fiber', function(id)
            return _G.fiber.create(function()
                _G.fiber.self():set_joinable(true)
                box.space.test:insert{id, _G.test_data}
            end)
        end)
        rawset(_G, 'test_f1', _G.make_txn_fiber(1))
        rawset(_G, 'test_f2', _G.make_txn_fiber(2))
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        -- Stall the limbo worker so it collects more than one CONFIRM in a
        -- single batch.
        box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', true)
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
    t.helpers.retrying({timeout = test_timeout}, function()
        t.assert(cg.server:grep_log('sending read%-view'))
    end)
    cg.server:exec(function()
        --
        -- Get the first 2 txns into WAL and unblock the relay from its write
        -- into _gc_consumers on Tarantool 3.3 and newer. On <= 3.2 there is no
        -- _gc_consumers. Which means on older versions the relay has already
        -- started read-view creation at this point.
        --
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        box.ctl.wal_sync()
        --
        -- The relay must have started the read-view creation. Make a newer txn
        -- which must not end up in the limbo's checkpoint.
        --
        _G.test_f2:wakeup()
        local f3 = _G.make_txn_fiber(3)
        box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', false)
        t.assert((_G.test_f1:join()))
        t.assert((_G.test_f2:join()))
        t.assert((f3:join()))
    end)
    cg.replica:wait_until_ready()
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

--
-- gh-11790 and gh-11180: the replica on join makes a journal sync to get the
-- vclock of the read-view that it is going to receive. It also collects the
-- limbo checkpoint to know the confirmed LSN of the last synchro txn.
--
-- It might happen that those 2 values won't be in sync. The master might do
-- the journal sync, and only then, eventually, the last one or many synchro
-- txns get CONFIRMs. The replica wouldn't have the lsns of these CONFIRMs, but
-- would have the transactions confirmed by them. Which is ok.
--
-- The problem is that later the replica would receive those CONFIRMs while
-- catching up on master's xlogs. And it must not treat this as an error, that
-- the received CONFIRMs <= already known confirm LSN.
--
g.test_limbo_checkpoint_confirm_after_wal_sync = function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local function make_txn_fiber(id)
            return _G.fiber.create(function()
                _G.fiber.self():set_joinable(true)
                box.space.test:insert{id, _G.test_data}
            end)
        end
        rawset(_G, 'test_f1', make_txn_fiber(1))
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', true)
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
    t.helpers.retrying({timeout = test_timeout}, function()
        t.assert(cg.server:grep_log('sending read%-view'))
    end)
    cg.server:exec(function()
        --
        -- The synchro txn gets written to WAL and the relay creates a
        -- read-view. But a CONFIRM isn't written yet.
        --
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        box.ctl.wal_sync()
        --
        -- Now the CONFIRM gets written. After read-view creation and journal
        -- sync for the replica.
        --
        box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', false)
        t.assert((_G.test_f1:join()))
    end)
    cg.replica:wait_until_ready()
    --
    -- Ensure the replication still works.
    --
    cg.server:exec(function()
        box.space.test:insert{2}
    end)
    cg.replica:wait_for_vclock_of(cg.server)
    cg.replica:exec(function()
        t.assert(box.space.test:get{2})
    end)
end
