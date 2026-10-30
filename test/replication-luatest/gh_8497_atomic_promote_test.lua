local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group('gh-8497-atomic-promote')

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new({})
    cg.box_cfg = {
        replication_timeout = 0.1,
        replication = {
            server.build_listen_uri('server1', cg.replica_set.id),
            server.build_listen_uri('server2', cg.replica_set.id),
        },
    }
    cg.box_cfg.election_mode = 'candidate'
    cg.server1 = cg.replica_set:build_and_add_server{
        alias = 'server1',
        box_cfg = cg.box_cfg,
    }
    cg.box_cfg.election_mode = 'voter'
    cg.server2 = cg.replica_set:build_and_add_server{
        alias = 'server2',
        box_cfg = cg.box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.server1:wait_for_election_leader()
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

g.test_election_promote_finishes_in_one_term = function(cg)
    cg.server2:update_box_cfg{election_mode = 'candidate'}
    local term = cg.server1:get_election_term()
    t.assert_equals(term, cg.server2:get_election_term(),
                    'The cluster is stable')
    local ok, err = cg.server2:exec(function()
        local fiber = require('fiber')
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
        local fib = fiber.new(box.ctl.promote)
        fib:set_joinable(true)
        fiber.sleep(2 * box.cfg.replication_timeout)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        return fib:join()
    end)
    t.assert_equals({ok, err}, {true, nil}, 'No error in promote')
    cg.server2:wait_for_election_leader()
    t.assert_equals(term + 1, cg.server1:get_election_term(),
                    'The term is bumped once')
    t.assert_equals(term + 1, cg.server2:get_election_term(),
                    'The term is bumped once')
end
