local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('replication_linearizable_quorum')

g.before_all(function(cg)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        memtx_use_mvcc_engine = true,
        election_mode = 'candidate',
        replication_timeout = 0.1,
        replication = {
            server.build_listen_uri('server1', cg.replica_set.id),
            server.build_listen_uri('server2', cg.replica_set.id),
        },
    }
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = box_cfg,
    }
    box_cfg.election_mode = 'voter'
    cg.server2 = cg.replica_set:build_and_add_server{
        alias = 'server2',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.server1:wait_for_election_leader()
    cg.server1:exec(function()
        box.schema.space.create('sync', {is_sync = true})
        box.space.sync:create_index('pk')
    end)
    cg.server1:wait_for_downstream_to(cg.server2)
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

g.test_config_option = function()
    g.server1:exec(function()
        local default = box.cfg.replication_linearizable_quorum
        t.assert_equals(default, 'N - Q + 1')
        --
        -- Prohibited formulas.
        --
        local broken_quorum_values = {
            'Q - 1', 'Q + 1', '2*Q', 'N - Q', 'N + Q', 'N - 1', 'N / 2',
            'Q / 2', '2*N', '3*N', 'N + 1', '0', '-1', 0, -1, 32
        }
        for _, value in ipairs(broken_quorum_values) do
            local ok = pcall(box.cfg, {replication_linearizable_quorum = value})
            t.assert_not(ok, value)
        end
        --
        -- Allowed formulas.
        --
        local valid_quorum_values = {
            '1', 'N', 'Q', 'N - Q + 1', '(N + Q) / 2',
            'math.max(1, N - Q + 1)', 'math.min(N, Q + 2)', 'math.ceil(N / 2)',
            'math.floor((N + Q) / 2)', 'math.max(Q, N / 2)',
            'math.min(N, Q)', 'Q + (N - Q) / 2', 1, 31
        }
        for _, value in ipairs(valid_quorum_values) do
            local ok = pcall(box.cfg, {replication_linearizable_quorum = value})
            t.assert(ok, value)
        end
        box.cfg{replication_linearizable_quorum = default}
    end)
end

g.test_out_bound_when_changing_synchro_quorum = function()
    -- Restart the server, so that none of the old update messages are grepped.
    g.server1:restart()
    local update_msg = 'update replication_linearizable_quorum = %d'
    local warn_msg = 'the formula for replication_linearizable_quorum ' ..
                     'evaluated to %d'
    --
    -- The default formula evaluates to negative value, when Q > N.
    --
    g.server1:exec(function()
        t.assert_equals(box.cfg.replication_linearizable_quorum, 'N - Q + 1')
        rawset(_G, 'old_synchro_quorum', box.cfg.replication_synchro_quorum)
        box.cfg{replication_synchro_quorum = 3}
    end)
    t.assert(g.server1:grep_log(string.format(warn_msg, 0)))
    t.assert(g.server1:grep_log(string.format(update_msg, 1)))
    g.server1:exec(function()
        box.cfg{replication_synchro_quorum = _G.old_synchro_quorum}
    end)

    --
    -- Some user formulas are valid, when Q <= N, but becomes > 31, when Q > N.
    --
    g.server1:exec(function()
        rawset(_G, 'old_lin_quorum', box.cfg.replication_linearizable_quorum)
        box.cfg{replication_linearizable_quorum = '31 * Q / N'}
        box.cfg{replication_synchro_quorum = 3}
    end)
    t.assert(g.server1:grep_log(string.format(warn_msg, 46)))
    t.assert(g.server1:grep_log(string.format(update_msg, 31)))
    g.server1:exec(function()
        box.cfg{replication_linearizable_quorum = _G.old_lin_quorum}
        box.cfg{replication_synchro_quorum = _G.old_synchro_quorum}
        _G.old_synchro_quorum = nil
        _G.old_lin_quorum = nil
    end)
end
