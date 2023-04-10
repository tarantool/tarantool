local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('gh-5272')

local run_before_cfg = [[
    rawset(_G, 'test_states', {})
    box.ctl.on_recovery_state(function(state)
        table.insert(test_states, state)
    end)
]]

g.before_all(function(cg)
    cg.cluster = cluster:new({})
    cg.server = cg.cluster:build_and_add_server({
        alias = 'server',
        env = {
            ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = run_before_cfg,
        },
    })
    cg.replica = cg.cluster:build_and_add_server({
        alias = 'replica',
        box_cfg = {
            replication = cg.server.net_box_uri,
        },
        env = {
            ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = run_before_cfg,
        },
    })
end)

g.after_all(function(cg)
    cg.cluster:drop()
end)

local function is_state_reached(server, state)
    return server:exec(function(state)
        for _, s in pairs(_G.test_states) do
            if s == state then
                return true
            end
        end
        return false
    end, {state})
end

local function wait_state(server, state)
    t.helpers.retrying({}, function()
        t.assert(is_state_reached(server, state),
                 'State ' .. state .. ' triggers fired')
    end)
end

local function assert_state_not_reached(server, state)
    t.assert(not is_state_reached(server, state),
             'State ' .. state .. ' not reached')
end

local function wait_all_states(server)
    wait_state(server, 'snapshot_recovered')
    -- wal_recovered and indexes_built can come in any order.
    wait_state(server, 'wal_recovered')
    wait_state(server, 'indexes_built')
    wait_state(server, 'synced')
end

g.test_recovery_stages = function(cg)
    -- Test stages on bootstrap.
    cg.server:start()
    wait_all_states(cg.server)
    cg.server:stop()
    -- Test same stages on recovery.
    cg.server:start()
    wait_all_states(cg.server)
    -- Test the stages on replica join.
    cg.replica:start()
    wait_all_states(cg.replica)
    cg.replica:stop()
    -- Test sync state.
    cg.replica.box_cfg = {
        replication_sync_timeout = 0.01,
        replication_connect_timeout = 0.1,
        replication_timeout = 0.1,
        replication = {
            cg.server.net_box_uri,
            server.build_listen_uri('non-existent-uri'),
        },
        bootstrap_strategy = 'legacy',
    }
    cg.replica:start()
    wait_state(cg.replica, 'snapshot_recovered')
    wait_state(cg.replica, 'wal_recovered')
    wait_state(cg.replica, 'indexes_built')
    assert_state_not_reached(cg.replica, 'synced')
    cg.replica:exec(function()
        box.cfg{
            replication_connect_quorum = 1,
        }
    end)
    wait_state(cg.replica, 'synced')
    -- Check that triggers on 'synced' stage are executed only once.
    cg.replica:exec(function()
        local num_events = #_G.test_states
        local old_repl = box.cfg.replication
        box.cfg{replication = ''}
        box.cfg{replication = old_repl}
        t.assert_equals(#_G.test_states, num_events,
                        "Triggers on sync aren't re-run")
    end)
end
