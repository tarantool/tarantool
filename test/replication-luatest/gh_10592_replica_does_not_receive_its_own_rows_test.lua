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

g.after_each(function(cg)
    cg.cluster:drop()
end)

g.before_test('test_master_falls_and_loses_xlogs', function(cg)
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

g.before_test('test_new_connections_while_waiting_for_own_rows', function(cg)
    t.tarantool.skip_if_not_debug()

    cg.cluster = cluster:new({})

    cg.replica = {}
    cg.replica_uri = {
        server.build_listen_uri('replica1', cg.cluster.id),
        server.build_listen_uri('replica2_proxy'),
    }
    cg.replica_cfg = {}

    local master_uri = server.build_listen_uri('master', cg.cluster.id)

    for i = 1, 2 do
        cg.replica_cfg[i] = {
            bootstrap_strategy = 'config',
            bootstrap_leader = master_uri,
            replication = { master_uri, cg.replica_uri[i], },
        }
        cg.replica[i] = cg.cluster:build_and_add_server({
            alias = string.format('replica%d', i),
            box_cfg = cg.replica_cfg[i],
        })
    end

    cg.master = cg.cluster:build_and_add_server({
        alias = 'master',
        box_cfg = {
            bootstrap_strategy = 'config',
            bootstrap_leader = master_uri,
            replication = {
                master_uri, cg.replica_uri[1], cg.replica_uri[2],
            },
            replication_connect_timeout = 1,
        },
    })

    -- We want replica2 to be specified in replication on the master,
    -- but the connection could not be established.
    cg.replica2_proxy = proxy:new({
        client_socket_path = cg.replica_uri[2],
        server_socket_path =
            server.build_listen_uri('replica2', cg.cluster.id),
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

-- Test that new connections are taken into account while the node is waiting
-- for own rows. In this test, replica2 connects to the master while it waits
-- for its own rows. The master must take into account that there is also a
-- second row, located uniquely on replica2. After this, syncing with replica1
-- alone should not be enough to leave the state waiting_for_own_rows mode,
-- master must sync with replica1 to get all its own rows back.
g.test_new_connections_while_waiting_for_own_rows = function(cg)
    -- Both replicas get the first row.
    cg.master:exec(function()
        box.snapshot()
        box.space.test:insert{1}
    end)
    wait_pair_sync(cg.replica[1], cg.master)
    -- But only the second replica gets the second row.
    cg.replica[1]:update_box_cfg({ replication = { cg.replica_uri[1], } })
    cg.master:exec(function() box.space.test:insert{2} end)
    wait_pair_sync(cg.replica[2], cg.master)

    cg.master:stop()
    -- Return replica1 to initial cfg (just to make wait_pair_sync work).
    cg.replica[1]:update_box_cfg({
        replication = cg.replica_cfg[1].replication,
        replication_connect_timeout = 1,
    })
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
    grep_log(cg.master, "entering waiting_for_own_rows mode")
    cg.master:exec(function(wait_timeout)
        t.helpers.retrying({ timeout = wait_timeout }, function()
            t.assert_equals(box.info.status, "waiting_for_own_rows")
        end)
    end, { wait_timeout })
    cg.replica2_proxy:resume()
    -- Let's wait until the master receives the ballot from replica2
    -- and update self rows max lsn. Otherwise the master could
    -- exit waiting_for_own_rows too soon.
    local replica2_id = cg.replica[2]:get_instance_id()
    cg.master:exec(function(replica2_id, wait_timeout)
        t.helpers.retrying({ timeout = wait_timeout }, function()
            t.assert_not_equals(
                box.info.replication[replica2_id].upstream.status, 'sync')
        end)
    end, { replica2_id, wait_timeout })
    cg.replica[1]:exec(function()
        box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', false)
    end)
    wait_pair_sync(cg.replica[1], cg.master)
    -- Syncing with replica1 is not enough to leave waiting_for_own_rows mode.
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
    grep_log(cg.master, "leaving waiting_for_own_rows mode")
    grep_log(cg.master, "ready to accept requests")
    -- Сheck that the master actually received these rows.
    cg.master:exec(function()
        t.assert_not_equals(box.space.test:get{1}, nil)
        t.assert_not_equals(box.space.test:get{2}, nil)
    end)
end

g.after_test('test_new_connections_while_waiting_for_own_rows', function()
    g.replica2_proxy:stop()
end)
