local t = require('luatest')
local cluster = require('test.luatest_helpers.cluster')
local server = require('test.luatest_helpers.server')

local fio = require('fio')

local g = t.group('gh-6966-readonly-bootstrap')

g.before_all(function(cg)
    cg.cluster = cluster:new({})

    local cfg = {
        replication_timeout = 0.1,
        replication = server.build_instance_uri('master'),
    }
    -- XXX: the order of add_server() is important. First add replica, then
    -- master. This way they are stopped by cluster:stop() in correct order,
    -- and the test runs 3 seconds faster. See gh-6820 (comment) for details:
    -- https://github.com/tarantool/tarantool/issues/6820#issuecomment-1082914500
    cg.replica = cg.cluster:build_and_add_server({
        alias = 'replica',
        box_cfg = cfg
    })
    cg.master = cg.cluster:build_and_add_server({
        alias = 'master',
        box_cfg = cfg
    })
    cg.master:start()
end)

g.after_all(function(cg)
    cg.cluster:drop()
end)

-- Test that joining replica tolerates ER_READONLY errors from master, and joins
-- once it becomes writeable.
g.test_ro_bootstrap = function(cg)
    cg.master:eval('box.cfg{read_only = true}')
    cg.replica:start({wait_for_readiness = false})
    -- The replica isn't booted yet so any attempts to eval box.cfg.log on it
    -- result in an error (net_box is not connected).
    local logfile = fio.pathjoin(cg.replica.workdir, 'replica.log')
    t.helpers.retrying({}, function()
        t.assert(cg.replica:grep_log('ER_READONLY', nil, {filename = logfile}),
                 'Replica receives the ER_READONLY error')
    end)
    cg.master:exec(function()
        require('luatest').assert(box.space._cluster:count() == 1,
                                  'No join while master is read-only')
    end)
    cg.master:eval('box.cfg{read_only = false}')
    cg.replica:wait_for_readiness()
    t.helpers.retrying({}, function()
        cg.master:exec(function()
            require('luatest').assert(box.space._cluster:count() == 2,
                                      'Join after master became writeable')
        end)
        cg.replica:assert_follows_upstream(1)
    end)
end
