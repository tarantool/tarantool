local t = require('luatest')
local cluster = require('luatest.replica_set')
local fio = require('fio')
local server = require('luatest.server')

local g = t.group('replica-does-not-receive-its-own-rows')

local wait_timeout = 20

local function wait_pair_sync(server1, server2)
    -- Without retrying it fails sometimes when vclocks are empty and both
    -- instances are in 'connect' state instead of 'follow'.
    t.helpers.retrying({timeout = wait_timeout}, function()
        server1:wait_for_vclock_of(server2)
        server2:wait_for_vclock_of(server1)
        server1:assert_follows_upstream(server2:get_instance_id())
        server2:assert_follows_upstream(server1:get_instance_id())
    end)
end

g.before_each(function(cg)
    cg.cluster = cluster:new({})

    local box_cfg = {
        replication = {
            server.build_listen_uri('master', cg.cluster.id),
            server.build_listen_uri('replica', cg.cluster.id),
        },
        replication_timeout = 0.1,
    }
    cg.master = cg.cluster:build_and_add_server({
        alias = 'master',
        box_cfg = box_cfg
    })
    cg.replica = cg.cluster:build_and_add_server({
        alias = 'replica',
        box_cfg = box_cfg
    })
    cg.cluster:start()
    cg.master:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
    end)
    wait_pair_sync(cg.replica, cg.master)
end)

g.after_each(function(cg)
    cg.cluster:drop()
end)

local function grep_log(server, str)
    local logfile = fio.pathjoin(server.workdir, server.alias .. '.log')
    t.helpers.retrying({ timeout = wait_timeout }, function()
        t.assert(server:grep_log(str, nil, {filename = logfile}))
    end)
end

g.test_master_falls_and_loses_xlogs = function(cg)
    cg.master:exec(function()
        box.snapshot()
        box.space.test:insert{1}
    end)
    wait_pair_sync(cg.replica, cg.master)
    cg.master:stop()
    local xlog = fio.glob(fio.pathjoin(cg.master.workdir, '*.xlog'))
    for _, file in pairs(xlog) do fio.unlink(file) end
    cg.master:restart()

    grep_log(cg.master, "entering waiting_for_own_rows mode")
    wait_pair_sync(cg.replica, cg.master)

    grep_log(cg.master, "leaving waiting_for_own_rows mode")
    grep_log(cg.master, "ready to accept requests")

    cg.master:exec(function()
        t.assert_not_equals(box.space.test:get{1}, nil)
    end)
end
