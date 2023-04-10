local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local fio = require('fio')

local g = t.group('gh-6966-readonly-bootstrap')

g.before_all(function(cg)
    cg.cluster = cluster:new({})

    local cfg = {
        replication_timeout = 0.1,
        replication = {
            server.build_listen_uri('master', cg.cluster.id),
        },
    }
    cg.master = cg.cluster:build_and_add_server({
        alias = 'master',
        box_cfg = cfg
    })
    cfg.replication[2] = server.build_listen_uri('replica', cg.cluster.id)
    cg.replica = cg.cluster:build_and_add_server({
        alias = 'replica',
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
    cg.replica:start({wait_until_ready = false})
    -- The replica isn't booted yet so any attempts to eval box.cfg.log on it
    -- result in an error (net_box is not connected).
    local logfile = fio.pathjoin(cg.replica.workdir, 'replica.log')
    t.helpers.retrying({}, function()
        t.assert(cg.replica:grep_log('ER_READONLY', nil, {filename = logfile}),
                 'Replica receives the ER_READONLY error')
    end)
    cg.master:exec(function()
        t.assert(box.space._cluster:count() == 1,
                 'No join while master is read-only')
    end)
    cg.master:eval('box.cfg{read_only = false}')
    cg.replica:wait_until_ready()
    t.helpers.retrying({}, function()
        cg.master:exec(function()
            t.assert(box.space._cluster:count() == 2,
                     'Join after master became writeable')
        end)
        cg.replica:assert_follows_upstream(1)
    end)
end
