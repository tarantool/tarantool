local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group()

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    cg.box_cfg = {
        replication = {
            server.build_listen_uri('server1', cg.replica_set.id),
            server.build_listen_uri('server2', cg.replica_set.id),
        },
        replication_timeout = 0.1,
    }
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.before_test('test_concurrent_promote_after_box_cfg', function(cg)
    cg.box_cfg.bootstrap_strategy = 'auto'
    cg.box_cfg.election_mode = 'off'
    for i = 1, 2 do
        local alias = 'server' .. i
        cg[alias] = cg.replica_set:build_and_add_server{
            alias = alias,
            box_cfg = cg.box_cfg,
        }
    end
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    for i = 1, 2 do
        local alias = 'server' .. i
        cg[alias]:exec(function()
            box.cfg{election_mode = 'manual'}
        end)
    end
end)

-- Check that concurrent invocations of `box.ctl.promote` after `box.cfg` are
-- handled correctly (gh-11708).
g.test_concurrent_promote_after_box_cfg = function(cg)
    cg.server1:exec(function()
        local fiber = require('fiber')

        local f = fiber.new(box.ctl.promote)
        f:set_joinable(true)
        local ok = pcall(box.ctl.promote)
        t.assert(ok)
        local err
        ok, err = f:join()
        t.assert_not(ok)
        local msg = 'box.ctl.promote/demote does not support simultaneous ' ..
                    'invocations'
        t.assert_equals(err.message, msg)
    end)
end

g.before_test('test_concurrent_promote_during_box_cfg', function(cg)
    cg.box_cfg.bootstrap_leader = cg.box_cfg.replication[1]
    cg.box_cfg.bootstrap_strategy = 'config'
    cg.box_cfg.election_mode = 'manual'
    local run_before_cfg = [[
        local fiber = require('fiber')

        rawset(_G, "promote_ok", false)
        rawset(_G, "promote_err", {})
        fiber.new(function()
            while box.info.status ~= 'running' do
                fiber.yield()
            end
            local ok, err = pcall(box.ctl.promote)
            _G.promote_ok = ok
            _G.promote_err = err
        end)
    ]]
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = cg.box_cfg,
        env = {
            ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = run_before_cfg,
        }
    }
    cg.server2 = cg.replica_set:build_and_add_server{
        alias = 'server2',
        box_cfg = cg.box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
end)

-- Check that concurrent invocations of `box.ctl.promote` during `box.cfg` are
-- handled correctly (gh-11703).
g.test_concurrent_promote_during_box_cfg = function(cg)
    cg.server1:exec(function()
        t.assert_not(_G.promote_ok)
        local msg = 'box.ctl.promote() is already running'
        t.assert_equals(_G.promote_err.message, msg)
        t.assert_equals(box.info.election.leader, box.info.id)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        end)
    end)
end
