local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local fio = require('fio')
local socket = require('socket')
local proxy = require('luatest.replica_proxy')

local g =
    t.group('gh-11156-calculation-of-sync-quorum-in-config-and-supervised')
--
-- gh-11156:
-- Calculation of sync quorum in 'config'/'supervised' bootstrap modes.
--
local wait_timeout = 20

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()

    cg.cluster = cluster:new({})

    local box_cfg = {
        replication = {
            server.build_listen_uri('master', cg.cluster.id),
            server.build_listen_uri('replica1', cg.cluster.id),
            server.build_listen_uri('replica2', cg.cluster.id),
            server.build_listen_uri('replica3', cg.cluster.id),
        },
        bootstrap_strategy = 'supervised',
        -- Retry bootstrap immediately.
        replication_timeout = 0.1,
    }

    -- There will be some non-booted nodes,
    -- so the net box will not be able to connect to them.
    local console_listen = "require('console').listen('unix/:%s')\n"

    -- Bootstrap master.
    cg.master = cg.cluster:build_and_add_server({
        alias = 'master',
        box_cfg = box_cfg,
    })
    cg.master_admin = fio.pathjoin(cg.master.workdir, 'master.admin')
    cg.master.env.TARANTOOL_RUN_BEFORE_BOX_CFG =
        string.format(console_listen, cg.master_admin)

    -- Booted, non-replicable.
    cg.non_synced_replicas = { 'replica2', 'replica3' }
    for _, replica_name in ipairs(cg.non_synced_replicas) do
        cg[replica_name] = cg.cluster:build_and_add_server({
            alias = replica_name,
            box_cfg = box_cfg,
        })
        cg[replica_name].env.TARANTOOL_RUN_BEFORE_BOX_CFG =
            -- Stop the relay so that replica1 cannot
            -- synchronize with replica2, replica3.
            "box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', true)\n"
    end

    -- Set low value to immediately go into an 'orphan' status.
    box_cfg.replication_sync_timeout = 0.001
    box_cfg.replication[1] = server.build_listen_uri('master_proxy')

    cg.replica1 = cg.cluster:build_and_add_server({
        alias = 'replica1',
        box_cfg = box_cfg,
    })
    -- Set `log_level` to "debug" to track appliers connection,
    -- this will be useful to guarantee a certain sync quorum in the test.
    cg.replica1.box_cfg.log_level = 'debug'

    cg.master_proxy = proxy:new({
        client_socket_path = server.build_listen_uri('master_proxy'),
        server_socket_path = server.build_listen_uri('master', cg.cluster.id),
    })
    t.assert(cg.master_proxy:start({force = true}))
    cg.master_proxy:pause()

    cg.cluster:start({wait_until_ready = false})
end)

g.after_each(function(cg)
    cg.cluster:drop()
end)

local function connect_console(sockname)
    local sock, err = socket.tcp_connect('unix/', sockname)
    t.assert_equals(err, nil, 'Connection successful')
    local greeting = sock:read(box.iproto.GREETING_SIZE, wait_timeout)
    t.assert_str_contains(greeting, 'Tarantool', 'Connected to console')
    t.assert_str_contains(greeting, 'Lua console', 'Connected to console')
    return sock
end

local function make_bootstrap_leader(sock)
    sock:write('box.ctl.make_bootstrap_leader()\n', wait_timeout)
    local response = sock:read(8, wait_timeout)
    t.assert_equals(response, '---\n...\n', 'The call succeeded')
end

local function wait_uuid(server)
    local logfile = fio.pathjoin(server.workdir, server.alias .. '.log')
    t.helpers.retrying({ timeout = wait_timeout }, function()
        -- In debug logging mode the file size may exceed the default value
        -- `bytes_num`. Therefore, let's set some large value manually.
        t.assert(server:grep_log(
            "instance uuid", 10000000, {filename = logfile}))
    end)
end

local function wait_applier_connected(server, upstream)
    local logfile = fio.pathjoin(server.workdir, server.alias .. '.log')
    local pattern =
        string.format("%s%%.sock[^\n]*D> => CONNECTED", upstream.alias)
    t.helpers.retrying({ timeout = wait_timeout }, function()
        -- In debug logging mode the file size may exceed the default value
        -- `bytes_num`. Therefore, let's set some large value manually.
        t.assert(server:grep_log(
            pattern, 10000000, {filename = logfile}))
    end)
end

g.test_calculation_of_sync_quorum = function(cg)
    wait_uuid(cg.master)

    local master_sock = t.helpers.retrying(
        { timeout = wait_timeout }, connect_console, cg.master_admin)
    make_bootstrap_leader(master_sock)
    master_sock:close()

    -- Wait until the nodes get bootstraped.
    cg.master:wait_until_ready()
    for _, replica_name in ipairs(cg.non_synced_replicas) do
        cg[replica_name]:wait_until_ready()
    end

    -- Wait for replica1 to connect to replica2, replica3 before connecting
    -- to the bootstrap master to ensure that replica2, replica3
    -- is counted in the sync quorum.
    wait_applier_connected(cg.replica1, cg.replica2)
    wait_applier_connected(cg.replica1, cg.replica3)
    cg.master_proxy:resume()
    -- Now cg.replica1 exits `replicaset_connect` with `state->connected = 4`,
    -- `state->booting: 1` so `replication_sync_quorum_auto` will be set to 3.

    -- We need exactly 2 non-replicable replicas (replica2, replica3),
    -- one is not enough, because of the second bug gh-11157. If we had only
    -- one non-replicable replica, then `replication_sync_quorum_auto` would be
    -- 2, and replica1 would consider itself to be in sync with the master and
    -- with ITSELF, DESPITE THE FACT THAT IT ITSELF IS NOT BOOTED. So we use
    -- 2 non-replicable replicas here to separate the effects of one bug from
    -- the effects of the other (gh-11156 from gh-11157).

    -- Wait until replica1 get bootstraped.
    cg.replica1:wait_until_ready()

    -- Replica1 cannot synchronize with replica2, replica3,
    -- so it goes into an 'orphan' status.
    cg.replica1:exec(function(wait_timeout)
        t.helpers.retrying({ timeout = wait_timeout }, function()
            t.assert_equals(box.info.status, 'orphan')
        end)
    end, { wait_timeout })

    -- Shutdown.
    for _, replica_name in ipairs(cg.non_synced_replicas) do
        cg[replica_name]:exec(function()
            box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', false)
        end)
    end
end
