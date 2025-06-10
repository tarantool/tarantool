local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({box_cfg = {wal_queue_max_size = 1000}})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
    if cg.replica then
        cg.replica:drop()
    end
end)

--
-- gh-11180: some rollbacks could slip into a committed read-view due to the
-- ignorance of rollbacks happening with volatile non-submitted journal entries,
-- which were present in the read-view.
--
g.test_fetch_snapshot_during_rollback = function(cg)
    --
    -- Block WAL for the next entry, which will be a _gc_consumers txn for the
    -- new replica.
    --
    cg.server:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        t.assert_not(box.space._gc_consumers.index[0]:min())
    end)
    --
    -- Start the new replica so it attempts to join and gets blocked.
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
        local fiber = require('fiber')
        local data = string.rep('a', 1000)
        local timeout = 60
        local s = box.space.test
        --
        -- The replica is trying to register the consumer already.
        --
        t.helpers.retrying({timeout = timeout}, function()
            t.assert(box.space._gc_consumers.index[0]:min())
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        --
        -- Before a read-view is created for the replica, lets create some
        -- dangling txns.
        --
        local function make_txn_fiber(id)
            return fiber.create(function()
                fiber.self():set_joinable(true)
                s:replace{id, data}
            end)
        end
        local function join_with_error(f, expected_err)
            local ok, err = f:join()
            t.assert_not(ok)
            t.assert_covers(err:unpack(), expected_err)
        end
        local f1 = make_txn_fiber(1)
        local f2 = make_txn_fiber(2)
        local f3 = make_txn_fiber(3)
        --
        -- Let the _gc_consumers txn to commit, but block the new ones. The
        -- read-view will then include them and will try to wait for them to get
        -- committed.
        --
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        --
        -- Txn1 went to WAL while txn2 and txn3 are waiting in the WAL queue.
        -- Get them cancelled to trigger a cascading rollback which must be
        -- spotted by the read-view creation code.
        --
        f2:cancel()
        join_with_error(f2, {type = 'FiberIsCancelled'})
        join_with_error(f3, {name = 'CASCADE_ROLLBACK'})
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((f1:join()))
    end)
    --
    -- The replica is retrying the snapshot fetch and on the second attempt
    -- succeeds.
    --
    cg.replica:wait_until_ready()
    local function check_data()
        t.assert(box.space.test:get{1})
        t.assert_not(box.space.test:get{2})
        t.assert_not(box.space.test:get{3})
    end
    -- Both have the same data.
    cg.replica:exec(check_data)
    cg.server:exec(check_data)
    -- Cleanup.
    cg.replica:drop()
    cg.server:exec(function() box.space.test:drop() end)
end
