local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group()
local TIMEOUT = 60
local RECONNECT_TIMEOUT = 100000

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new({})
    cg.master = cg.replica_set:build_and_add_server({alias = 'master'})
    cg.replica = cg.replica_set:build_and_add_server({
        alias = 'replica',
        box_cfg = {
            read_only = true,
            replication = server.build_listen_uri('master', cg.replica_set.id),
            replication_timeout = 0.1,
        },
    })
    cg.replica_set:start()
end)

g.after_all(function(cg)
    pcall(cg.replica.exec, cg.replica, function()
        box.error.injection.set('ERRINJ_APPLIER_DESTROY_DELAY', false)
    end)
    cg.replica_set:drop()
end)

--
-- gh-13847: an applier fiber could be cancelled while handling an IO error,
-- and the cancellation would go unnoticed for the reconnect timeout duration.
--
-- An IO error makes an applier detach itself from its IO thread. This takes
-- time and yields. A cancellation during that can come, but is ignored, because
-- the applier anyway needs to finish the thread detach.
--
-- After the detach was done, the cancellation flag simply wasn't re-checked
-- before entering any next fiber sleep or yield.
--
g.test_stop_during_thread_detach = function(cg)
    -- The long reconnect pause is set only now. The instances start together,
    -- and the replica's first attempts to join can fail while the master is
    -- still bootstrapping. Those retries must stay quick.
    cg.replica:exec(function(reconnect_timeout)
        box.cfg{replication_reconnect_timeout = reconnect_timeout}
        box.error.injection.set('ERRINJ_APPLIER_DESTROY_DELAY', true)
    end, {RECONNECT_TIMEOUT})
    cg.master:stop()
    t.helpers.retrying({timeout = TIMEOUT}, function()
        t.assert(cg.replica:grep_log('applier data destruction is delayed'))
    end)
    cg.replica:exec(function(timeout)
        local fiber = require('fiber')
        local is_done = false
        local f = fiber.create(function()
            fiber.self():set_joinable(true)
            box.cfg{replication = {}}
            is_done = true
        end)
        -- Give box.cfg() time to cancel the applier fiber. Technically a single
        -- yield could be enough, but it would rely on the applier cancel
        -- happening before any other yields in box.cfg().
        fiber.sleep(0.01)
        t.assert_not(is_done)
        -- The injection was preventing the applier from doing its IO thread
        -- detach too fast and getting the cancel too late for the bug to
        -- happen.
        box.error.injection.set('ERRINJ_APPLIER_DESTROY_DELAY', false)
        -- Without a fix the applier would now finish the IO thread detach and
        -- proceed to reconnect backoff sleep despite being cancelled.
        t.assert_equals({f:join(timeout)}, {true})
        t.assert_equals(box.info.replication[1].upstream, nil)
    end, {TIMEOUT})
end
