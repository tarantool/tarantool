local t = require('luatest')
local common = require('test.replication-luatest.qpromote_common')

local g = common.make_test_group({nodes=5, quorum=3})

-- The test exercises the case when promote request is failed
-- to be written to disk on replica
g.test_transient_wal_error_on_promote_write_on_replica = function(g)
    local n1 = g.cluster.servers[1]
    local n2 = g.cluster.servers[2]

    n2:exec(function()
        box.error.injection.set(
            'ERRINJ_WAL_ERROR_ON_XROW_TYPE',
            box.iproto.type.RAFT_PROMOTE)
    end)
    common.errinj_wait(n2, 'ERRINJ_WAL_ERROR_ON_XROW_TYPE')

    common.promote(n1)

    common.errinj_wait(n2, "ERRINJ_WAL_WRITE_DISK")
    t.assert(n2:grep_log('ER_WAL_IO: Failed to write to disk'))

    n2:restart()

    n2:wait_for_vclock_of(n1)

    t.helpers.retrying({}, function ()
        common.ensure_healthy(g.cluster.servers)
    end)
end

-- The same as previous one but for promote write on master.
g.test_transient_wal_error_on_promote_write_on_master = function(g)
    local n1 = g.cluster.servers[1]
    local n2 = g.cluster.servers[2]
    local n3 = g.cluster.servers[3]

    -- Both n1 and n2 have hard time pushing out their promotes.
    common.spawn_stuck_promote(n1)
    common.spawn_stuck_promote(n2)

    n3:exec(function()
        box.error.injection.set(
            'ERRINJ_WAL_ERROR_ON_XROW_TYPE',
            box.iproto.type.RAFT_PROMOTE)
    end)

    common.spawn_promote(n3)

    t.helpers.retrying({}, function ()
        t.assert(n3:grep_log('Could not write a synchro request to WAL'))
    end)

    -- server panicked at this point
    n3:restart()

    common.promote(n3)

    local healthy = {
        g.cluster.servers[3],
        g.cluster.servers[4],
        g.cluster.servers[5],
    }
    t.helpers.retrying({}, function ()
        common.ensure_healthy(healthy)
    end)

    common.remove_wal_delay_on_xrow_type(n1)
    common.remove_wal_delay_on_xrow_type(n2)

    t.helpers.retrying({}, function ()
        common.ensure_healthy(g.cluster.servers)
    end)
end
