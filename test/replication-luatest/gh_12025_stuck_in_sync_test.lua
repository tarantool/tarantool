local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

--
-- gh-12025: master used to miss attaching the current timestamp to the
-- heartbeat message when replica subscribes and Raft is actively working.
--
g.before_all(function(cg)
    --
    -- The test can't use the replica_set LuaTest module. Because it requires
    -- the nodes to start together in fullmesh. And that sometimes leads to the
    -- replica connecting to the master too fast, when the master has already
    -- marked itself "ready", but didn't elect itself a Raft leader yet, thus
    -- staying "read-only". The replica then can't join, and doesn't seem to
    -- retry any time soon when replication_timeout is huge. The timeout being
    -- huge is required for this test to make sense, it can't be lowered.
    --
    -- The workaround is to start master first and alone. And then join the
    -- replica.
    --
    -- Master
    --
    local cfg = {
        replication_timeout = 10000,
        replication_reconnect_timeout = 0.1,
        election_mode = 'candidate',
        replication_synchro_timeout = 1000,
    }
    cg.master = server:new{
        alias = 'master',
        box_cfg = cfg,
    }
    cg.master:start()
    --
    -- Replica
    --
    cfg.election_mode = 'voter'
    cfg.replication = {cg.master.net_box_uri}
    cg.replica = server:new{
        alias = 'replica',
        box_cfg = cfg,
    }
    cg.replica:start()
end)

g.after_all(function(cg)
    cg.master:drop()
    cg.replica:drop()
end)

g.test_case = function(cg)
    local replication = {cg.master.net_box_uri, cg.replica.net_box_uri}
    cg.master:update_box_cfg{replication = replication}
    cg.replica:update_box_cfg{replication = replication}
    cg.master:wait_for_vclock_of(cg.replica)
    cg.replica:wait_for_vclock_of(cg.master)
    t.helpers.retrying({}, function()
        cg.master:assert_follows_upstream(cg.replica:get_instance_id())
        cg.replica:assert_follows_upstream(cg.master:get_instance_id())
    end)
end
