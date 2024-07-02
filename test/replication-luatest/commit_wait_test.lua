local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group()

--
-- gh-67: wal_mode='async' which was turned into a commit{wait=...} feature.
--
g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication_timeout = 0.1,
        replication = {
            server.build_listen_uri('master', cg.replica_set.id),
            server.build_listen_uri('replica', cg.replica_set.id),
        }
    }
    box_cfg.election_mode = 'candidate'
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = box_cfg,
    }
    box_cfg.election_mode = 'voter'
    cg.replica = cg.replica_set:build_and_add_server{
        alias = 'replica',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.master:exec(function()
        box.schema.space.create('test_sync', {is_sync = true})
        box.space.test_sync:create_index('pk')
    end)
    cg.replica:wait_for_vclock_of(cg.master)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.test_commit_wait_sync_txn = function(cg)
    cg.master:exec(function()
        box.begin()
        box.space.test_sync:replace{1}
        box.commit({wait = 'complete'})

        for _, mode in pairs{'submit'} do
            box.begin()
            box.space.test_sync:replace{2}
            t.assert(box.is_in_txn())
            t.assert_error_msg_contains(
                'Non-blocking commit of a synchronous txn', box.commit,
                {wait = mode})
            t.assert(not box.is_in_txn())
            t.assert_equals(box.space.test_sync:select(), {{1}})
        end
    end)
end
