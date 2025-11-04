local server = require('luatest.server')
local replica_set = require('luatest.replica_set')
local t = require('luatest')
local g = t.group()

--
-- gh-11836: there was a possible deadlock when a replica had
-- box.cfg.replication_synchro_queue_max_size smaller than the master, and it
-- was actually filled and exceeded before the master would send CONFIRM.
--
g.before_all(function(cg)
    cg.replica_set = replica_set:new()
    local replication = {
        server.build_listen_uri('master', cg.replica_set.id),
        server.build_listen_uri('replica', cg.replica_set.id),
    }
    local master_cfg = {
        replication = replication,
        replication_timeout = 0.1,
        election_mode = 'candidate',
        election_fencing_mode = 'off',
        replication_synchro_timeout = 1000,
        replication_synchro_quorum = 3,
    }
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = master_cfg,
    }
    local replica_cfg = {
        replication = replication,
        replication_timeout = 0.1,
        read_only = true,
        election_mode = 'voter',
        replication_synchro_queue_max_size = 1000,
    }
    cg.replica = cg.replica_set:build_and_add_server{
        alias = 'replica',
        box_cfg = replica_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function(max_size)
        rawset(_G, 'fiber', require('fiber'))
        rawset(_G, 'test_data', string.rep('a', max_size))
        rawset(_G, 'test_timeout', 60)

        box.ctl.wait_rw()
        local s = box.schema.create_space('test_sync', {is_sync = true})
        s:create_index('pk')
    end, {replica_cfg.replication_synchro_queue_max_size})
    cg.replica:wait_for_vclock_of(cg.master)
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

g.test_case = function(cg)
    cg.replica:exec(function()
        rawset(_G, 'test_events', {})
        local function test_on_replace(_, new)
            t.assert(new)
            local id = new[1]
            table.insert(_G.test_events, ('got %s'):format(id))
            box.on_commit(function()
                table.insert(_G.test_events, ('commit %s'):format(id))
            end)
            box.on_rollback(function()
                assert(not "reachable")
            end)
        end
        box.space.test_sync:on_replace(test_on_replace)
    end)
    --
    -- Master sends some txns to the replica.
    --
    cg.master:exec(function()
        rawset(_G, 'test_events', {})
        local function make_txn_fiber(id)
            return _G.fiber.new(function()
                _G.fiber.self():set_joinable(true)
                box.begin()
                box.on_commit(function()
                    table.insert(_G.test_events, ('commit %s'):format(id))
                end)
                box.on_rollback(function()
                    assert(not "reachable")
                end)
                box.space.test_sync:insert{id, _G.test_data}
                box.commit()
            end)
        end
        local lsn = box.info.lsn
        rawset(_G, 'test_f1', make_txn_fiber(1))
        rawset(_G, 'test_f2', make_txn_fiber(2))
        rawset(_G, 'test_f3', make_txn_fiber(3))
        t.helpers.retrying({timeout = _G.test_timeout}, function()
            t.assert_equals(box.info.lsn, lsn + 3)
        end)
    end)
    --
    -- First 2 enter the replica's limbo freely. The third one must enter it
    -- exceeding the size.
    --
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        t.assert_gt(box.info.synchro.queue.size,
                    box.cfg.replication_synchro_queue_max_size * 3)
    end)
    --
    -- The master sends CONFIRM for all 3 txns.
    --
    cg.master:exec(function()
        box.cfg{replication_synchro_quorum = 2}
        t.assert((_G.test_f1:join(_G.test_timeout)))
        t.assert((_G.test_f2:join(_G.test_timeout)))
        t.assert((_G.test_f3:join(_G.test_timeout)))
        t.assert_equals(_G.test_events, {'commit 1', 'commit 2', 'commit 3'})
    end)
    --
    -- The replica wasn't blocked and was able to receive the CONFIRM.
    --
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        t.assert_equals(_G.test_events, {
            'got 1', 'got 2', 'got 3',
            'commit 1', 'commit 2', 'commit 3'
        })
    end)
end
