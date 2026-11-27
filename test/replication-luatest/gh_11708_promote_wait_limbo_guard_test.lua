local t = require('luatest')
local server = require('luatest.server')
local g = t.group('gh_11708_promote_wait_limbo_guard')

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {
        election_mode = 'off',
        election_timeout = 120,
        replication_synchro_timeout = 120,
    }})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- The Raft worker runs election triggers before claiming the limbo. While a
-- trigger yields, the public caller can finish its Raft wait and start waiting
-- for the limbo. Neither promote nor demote may interfere with this wait.
--
g.test_wait_for_limbo_blocks_public_calls = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local entered, release = fiber.channel(1), fiber.channel(1)
        local function on_election()
            if box.info.election.state == 'leader' then
                entered:put(true)
                release:get()
            end
        end
        box.ctl.on_election(on_election)
        local promote = fiber.new(box.ctl.promote)
        promote:set_joinable(true)
        t.assert(entered:get(120))

        -- Resume the caller once with Raft already in the leader state. Its
        -- next yield must be in the limbo wait, since the worker is still held.
        local csw = promote:csw()
        promote:wakeup()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_gt(promote:csw(), csw)
        end)
        t.assert_equals(box.info.synchro.queue.owner, 0)
        t.assert_not(box.info.synchro.queue.busy)
        local term = box.info.election.term
        local concurrent = fiber.create(function()
            fiber.self():set_joinable(true)
            t.assert_error_covers({code = box.error.IN_ANOTHER_PROMOTE},
                                  box.ctl.promote)
            t.assert_error_covers({code = box.error.IN_ANOTHER_PROMOTE},
                                  box.ctl.demote)
        end)
        t.assert(concurrent:join(0))
        t.assert_equals(box.info.election.term, term)

        box.ctl.on_election(nil, on_election)
        release:put(true)
        t.assert(promote:join(120))
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.ctl.demote()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        t.assert_not(box.info.ro)
    end)
end

g.test_cancel_during_election_releases_guard = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        local term = box.info.election.term
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local promote = fiber.new(box.ctl.promote)
        promote:set_joinable(true)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_gt(box.info.election.term, term)
        end)
        t.assert_equals(box.info.election.state, 'follower')
        t.assert_error_covers({code = box.error.IN_ANOTHER_PROMOTE},
                              box.ctl.promote)
        t.assert_error_covers({code = box.error.IN_ANOTHER_PROMOTE},
                              box.ctl.demote)

        promote:cancel()
        local ok, err = promote:join(120)
        t.assert_not(ok)
        t.assert_equals(err.type, 'FiberIsCancelled')
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        box.ctl.wal_sync()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        box.ctl.promote()
        box.ctl.demote()
        t.assert_not(box.info.ro)
    end)
end

--
-- Cancelling the public caller does not cancel the Raft worker's PROMOTE WAL
-- write. Both public operations must still report the background promotion
-- until the worker finishes, even though no public call remains active.
--
g.test_background_promotion_blocks_public_calls = function(cg)
    t.tarantool.skip_if_not_debug()
    local promote_id = cg.server:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local promote = require('fiber').new(box.ctl.promote)
        promote:set_joinable(true)
        return promote:id()
    end)
    cg.server:play_wal_until_synchro_queue_is_busy()
    cg.server:exec(function(promote_id)
        local promote = require('fiber').find(promote_id)
        promote:cancel()
        local ok, err = promote:join(120)
        t.assert_not(ok)
        t.assert_equals(err.type, 'FiberIsCancelled')
        t.assert(box.info.synchro.queue.busy)

        local expected = {
            code = box.error.UNSUPPORTED,
            message = 'box.ctl.promote/demote() does not support execution ' ..
                      'during Raft background promotion',
        }
        t.assert_error_covers(expected, box.ctl.promote)
        t.assert_error_covers(expected, box.ctl.demote)

        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.owner, box.info.id)
            t.assert_not(box.info.synchro.queue.busy)
        end)
        box.ctl.promote()
        box.ctl.demote()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        t.assert_not(box.info.ro)
    end, {promote_id})
end
