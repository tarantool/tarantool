local common = require('test.replication-luatest.qpromote_common')

local g = common.make_test_group({nodes=5, quorum=3, skip_start=true})

-- This is a regression test. box_register_replica is executed in a transaction
-- that inserts into cluster space. on_replace_dd_cluster used to have a
-- possibility to yield which lead to transaction being aborted.
g.test_do_not_yield_in_box_register_replica = function(g)
    for _, server in ipairs(g.cluster.servers) do
        server.env['ERRINJ_LIMBO_WRITE_PROMOTE_SLEEP'] = 'true'
    end

    g.cluster:start()

end
