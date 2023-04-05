local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local fiber = require('fiber')

local g = t.group('gh-6036')

g.before_all(function(cg)
    cg.cluster = cluster:new({})

    cg.box_cfg = {
        replication = {
            server.build_listen_uri('r1'),
            server.build_listen_uri('r2'),
        },
        replication_timeout         = 0.1,
        replication_connect_quorum  = 1,
        election_mode               = 'manual',
        election_timeout            = 0.1,
        replication_synchro_quorum  = 1,
        replication_synchro_timeout = 0.1,
        log_level                   = 6,
    }

    cg.r1 = cg.cluster:build_and_add_server({
        alias = 'r1',
        box_cfg = cg.box_cfg
    })
    cg.r2 = cg.cluster:build_and_add_server({
        alias = 'r2',
        box_cfg = cg.box_cfg
    })
    cg.cluster:start()

    cg.r1:wait_for_vclock({[1] = 3})
    cg.r2:wait_for_vclock({[1] = 3})
    cg.r1:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
    end)
end)

g.after_each(function(cg)
    local leader = cg.cluster:get_leader()
    leader:exec(function() box.space.test:truncate() end)
end)

g.after_all(function(cg)
    cg.cluster:drop()
end)

local function update_replication(...)
    return box.cfg{replication = {...}}
end

local function select()
    return box.space.test:select{}
end

--
-- The test requires 3rd replica to graft in.
g.before_test("test_qsync_order", function(cg)
    cg.box_cfg.replication[3] = server.build_listen_uri("r3")
    cg.r3 = cg.cluster:build_and_add_server({
        alias = 'r3',
        box_cfg = cg.box_cfg
    })
    cg.r3:start()
    cg.r1:exec(update_replication, cg.box_cfg.replication)
    cg.r2:exec(update_replication, cg.box_cfg.replication)
end)

g.test_qsync_order = function(cg)
    cg.r3:wait_for_vclock_of(cg.r1)
    cg.r3:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
        box.space.test:insert{1}
    end)

    cg.r1:wait_for_vclock_of(cg.r3)
    cg.r2:wait_for_vclock_of(cg.r3)

    t.assert_equals(cg.r1:exec(select), {{1}})
    t.assert_equals(cg.r2:exec(select), {{1}})
    t.assert_equals(cg.r3:exec(select), {{1}})

    --
    -- Drop connection between r3 and r2.
    cg.r3:exec(update_replication, {
        server.build_listen_uri("r1"),
        server.build_listen_uri("r3"),
    })

    --
    -- Drop connection between r2 and r3.
    cg.r2:exec(update_replication, {
        server.build_listen_uri("r1"),
        server.build_listen_uri("r2"),
    })

    --
    -- Here we have the following scheme
    --
    --      r1 (WAL delay)
    --      /            \
    --    r3              r2
    --

    --
    -- Initiate disk delay in a bit tricky way: the next write will
    -- fall into forever sleep.
    cg.r1:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
    end)

    --
    -- Make r2 been a leader and start writting data, the PROMOTE
    -- request get queued on r3 and not yet processed, same time
    -- the INSERT won't complete either waiting for the PROMOTE
    -- completion first. Note that we enter r3 as well just to be
    -- sure the PROMOTE has reached it via queue state test.
    cg.r2:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
    end)
    t.helpers.retrying({}, function()
        t.assert(cg.r1:exec(function()
            return box.info.synchro.queue.busy == true
        end))
    end)
    cg.r2:exec(function()
        box.space.test:insert{2}
    end)

    --
    -- The r3 node has no clue that there is a new leader and continue
    -- writing data with obsolete term. Since r1 is delayed now
    -- the INSERT won't proceed yet but get queued.
    cg.r3:exec(function()
        box.space.test:insert{3}
    end)
    --
    -- Finally enable r1 back. Make sure the data from new r2 leader get
    -- writing while old leader's data ignored.
    cg.r1:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
    end)
    t.helpers.retrying({}, function()
        t.assert(cg.r1:exec(function()
            return box.space.test:get{2} ~= nil
        end))
    end)

    t.assert_equals(cg.r1:exec(select), {{1},{2}})
end

--
-- Drop the r3 replica, since it is no longer needed for this test.
g.after_test("test_qsync_order", function(cg)
    cg.box_cfg.replication[3] = nil
    cg.r1:exec(update_replication, cg.box_cfg.replication)
    cg.r2:exec(update_replication, cg.box_cfg.replication)
    cg.r3:drop()
    cg.r3 = nil
end)

g.test_promote_order = function(cg)
    --
    -- Make sure that while we're processing PROMOTE no other records
    -- get sneaked in via applier code from other replicas. For this
    -- sake initiate voting and stop inside wal thread just before
    -- PROMOTE get written. Another replica sends us new record and
    -- it should be dropped.
    cg.r1:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
    end)
    cg.r2:wait_for_vclock_of(cg.r1)

    --
    -- Drop connection between r1 and the rest of the cluster.
    -- Otherwise r1 might become Raft follower before attempting
    -- insert{4}.
    cg.r1:exec(function() box.cfg{replication=""} end)
    cg.r2:exec(function()
        box.cfg{replication = {}}
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 2)
        require('fiber').create(function() box.ctl.promote() end)
    end)
    t.helpers.retrying({}, function()
        t.assert(cg.r2:exec(function()
            return box.info.synchro.queue.busy
        end))
    end)
    t.assert(cg.r1:exec(function() return box.info.ro == false end))
    cg.r1:exec(function()
        box.space.test:insert{4}
    end)
    cg.r2:exec(function(replication)
        box.cfg{replication = replication}
    end, {cg.box_cfg.replication})
    -- Give r2 a chance to fetch the new tuple.
    fiber.sleep(cg.box_cfg.replication_timeout + 0.1)
    cg.r2:exec(function()
        t.assert(box.info.synchro.queue.busy == true)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        box.ctl.wait_rw()
    end)

    t.assert_equals(cg.r2:exec(select), {})
end
