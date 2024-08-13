local common = require('test.replication-luatest.qpromote_common')

local g = common.make_test_group({nodes=5, quorum=3})

-- Simple test that verifies cluster can be bootstrapped successfully
-- Some promotes are echanged during bootstrap so it still excersises
-- new logic added by the quorum promote
g.test_smoke = function(g)
    common.ensure_healthy(g.cluster.servers)
end
