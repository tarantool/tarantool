local t = require('luatest')
local cluster = require('test.luatest_helpers.cluster')
local server = require('test.luatest_helpers.server')
local fio = require('fio')

local g = t.group('gh_6126')

g.before_each(function(cg)
    cg.cluster = cluster:new({})
    local master_uri = server.build_instance_uri('master')
    local replica_uri = server.build_instance_uri('replica')
    local replication = {master_uri, replica_uri}
    local box_cfg = {
        listen = replica_uri,
        replication = replication,
        replication_timeout = 3,
    }
    cg.replica = cg.cluster:build_and_add_server(
        {alias = 'replica', box_cfg = box_cfg})

    box_cfg.listen = master_uri
    box_cfg.replication = {master_uri}
    cg.master = cg.cluster:build_and_add_server(
        {alias = 'master', box_cfg = box_cfg})

    cg.master:start()
end)

g.after_each(function(cg)
    cg.cluster.servers = nil
    cg.cluster:drop()
end)

g.test_auto_rejoin = function(cg)
    -- Test that joining replica tolerates ER_READONLY errors from master
    cg.master:exec(function() box.cfg{read_only = true} end)
    cg.replica:start({wait_for_readiness = false})
    local logfile = fio.pathjoin(cg.replica.workdir, 'replica.log')
    t.helpers.retrying({}, function()
        t.assert(cg.replica:grep_log('ER_READONLY', nil,
            {filename = logfile}), 'Can\'t modify data on a read-only'..
            'instance - box.cfg.read_only is true')

        t.assert(g.master:exec(function()
            return box.space._cluster:count() == 1
        end),  'No join while master is read-only')
    end)

    -- Test that replica joins after master became writeable
    cg.master:exec(function() box.cfg{read_only = false} end)
    cg.replica:wait_for_readiness()
    t.helpers.retrying({}, function()
        t.assert(g.master:exec(function()
            return box.space._cluster:count() == 2
        end), 'Join after master became writeable')
        cg.replica:assert_follows_upstream(1)
    end)
end
