local t = require('luatest')
local cluster = require('luatest.replica_set')
local fio = require('fio')
local server = require('luatest.server')
local proxy = require('luatest.replica_proxy')

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

    cg.replica = {}
    cg.replica_uri = {}
    cg.replica_cfg = {}

    for i = 1, 2 do
        local alias = string.format('replica%d', i)
        cg.replica_uri[i] = server.build_listen_uri(alias, cg.cluster.id)
        cg.replica_cfg[i] = {
            replication = {
                server.build_listen_uri('master', cg.cluster.id),
                cg.replica_uri[i],
            }
        }
        cg.replica[i] = cg.cluster:build_and_add_server({
            alias = alias,
            box_cfg = cg.replica_cfg[i]
        })
    end

    local replica2_proxy_uri = server.build_listen_uri('replica2_proxy')
    cg.master = cg.cluster:build_and_add_server({
        alias = 'master',
        box_cfg = {
            replication = {
                server.build_listen_uri('master', cg.cluster.id),
                cg.replica_uri[1],
                replica2_proxy_uri,
            }
        },
    })

    -- We want replica2 to be specified in replication on the master,
    -- but the connection could not be established.
    cg.replica2_proxy = proxy:new({
        client_socket_path = replica2_proxy_uri,
        server_socket_path = cg.replica_uri[2],
    })
    t.assert(cg.replica2_proxy:start({force = true}))

    cg.cluster:start()
    cg.master:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
    end)
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
    -- Both replicas get the first row.
    cg.master:exec(function()
        box.snapshot()
        box.space.test:insert{1}
    end)
    wait_pair_sync(cg.replica[1], cg.master)
    -- But only the second replica gets the second row.
    cg.replica[1]:update_box_cfg({replication = { cg.replica_uri[1], }})
    cg.master:exec(function() box.space.test:insert{2} end)
    wait_pair_sync(cg.replica[2], cg.master)

    cg.master:stop()
    -- Return replica1 to initial cfg (just to make wait_pair_sync work).
    cg.replica[1]:update_box_cfg(cg.replica_cfg[1])
    -- Truncate xlogs on master.
    local xlog = fio.glob(fio.pathjoin(cg.master.workdir, '*.xlog'))
    for _, file in pairs(xlog) do fio.unlink(file) end
    -- Stop replication from both replica1 and replica2.
    for i = 1, 2 do
        cg.replica[i]:exec(function()
            box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', true)
        end)
    end
    -- Stop proxy to prevent connection with replica2. The master will
    -- receive ballot only from replica1 and will not know about the 
    -- existence of the second row for some time.
    cg.replica2_proxy:pause()

    cg.master:restart()
    -- Master sees that replica1 has one row created by master itself
    -- before the crash, so it goes waiting_for_own_rows mode.
    --grep_log(cg.master, "entering waiting_for_own_rows mode")
    cg.master:exec(function(wait_timeout)
        t.helpers.retrying({ timeout = wait_timeout }, function()
            t.assert_equals(box.info.status, "waiting_for_own_rows")
        end)
    end, { wait_timeout })
    cg.replica2_proxy:resume()
    -- Let's give it some time to get the ballot from replica2
    -- and update self rows max lsn. No sleep would let the master
    -- exit waiting_for_own_rows too soon.
    require('fiber').sleep(3.0)
    -- Syncing with replica1 only shouldn't be enough because
    -- it doesn't have the second row.
    cg.replica[1]:exec(function()
        box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', false)
    end)
    wait_pair_sync(cg.replica[1], cg.master)
    -- Syncing with replica1 is not enough
    -- to leave waiting_for_own_rows mode.
    cg.master:exec(function()
        t.assert_equals(box.info.status, "waiting_for_own_rows")
    end)
    -- But syncing with replica2 is enough.
    cg.replica[2]:exec(function()
        box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', false)
    end)
    wait_pair_sync(cg.replica[2], cg.master)
    cg.master:exec(function()
        t.assert_equals(box.info.status, "running")
    end)
    --grep_log(cg.master, "leaving waiting_for_own_rows mode")
    --grep_log(cg.master, "ready to accept requests")
    -- Сheck that the master actually received these rows.
    cg.master:exec(function()
        t.assert_not_equals(box.space.test:get{1}, nil)
        t.assert_not_equals(box.space.test:get{2}, nil)
    end)
end
