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
local SOCKET_TIMEOUT = 5

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
        -- Retry bootstrap immediately
        replication_timeout = 0.1,
    }

    -- There will be some non-booted nodes,
    -- so the net box will not be able to connect to them
    local console_listen = "require('console').listen('unix/:%s')\n"

    -- Bootstrap master
    cg.master = cg.cluster:build_and_add_server({
        alias = 'master',
        box_cfg = box_cfg,
    })
    cg.master_admin = fio.pathjoin(cg.master.workdir, 'master.admin')
    cg.master.env.TARANTOOL_RUN_BEFORE_BOX_CFG =
        string.format(console_listen, cg.master_admin)

    cg.non_synced_replicas = { 'replica2', 'replica3' }
    for _, replica_name in ipairs(cg.non_synced_replicas) do
        cg[replica_name] = cg.cluster:build_and_add_server({
            alias = replica_name,
            box_cfg = box_cfg,
        })
        cg[replica_name].env.TARANTOOL_RUN_BEFORE_BOX_CFG =
            -- Stop the relay so that replica1 cannot synchronize with replica2
            "box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', true)\n"
    end

    -- Set low value to immediately go into an 'orphan' status
    box_cfg.replication_sync_timeout = 0.001
    box_cfg.replication[1] = server.build_listen_uri('master_proxy')

    cg.replica1 = cg.cluster:build_and_add_server({
        alias = 'replica1',
        box_cfg = box_cfg,
    })

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
    local greeting = sock:read(box.iproto.GREETING_SIZE, SOCKET_TIMEOUT)
    t.assert_str_contains(greeting, 'Tarantool', 'Connected to console')
    t.assert_str_contains(greeting, 'Lua console', 'Connected to console')
    return sock
end

local function make_bootstrap_leader(sock)
    sock:write('box.ctl.make_bootstrap_leader()\n', SOCKET_TIMEOUT)
    local response = sock:read(8, SOCKET_TIMEOUT)
    t.assert_equals(response, '---\n...\n', 'The call succeeded')
end

local function wait_uuid(server)
    local logfile = fio.pathjoin(server.workdir, server.alias .. '.log')
    t.helpers.retrying({}, function()
        t.assert(server:grep_log("instance uuid", nil, {filename = logfile}))
    end)
end

g.test_calculation_of_sync_quorum = function(cg)
    wait_uuid(cg.master)

    local master_sock = t.helpers.retrying({}, connect_console, cg.master_admin)
    make_bootstrap_leader(master_sock)
    master_sock:close()

    -- Wait until the nodes get bootstraped
    cg.master:wait_until_ready()
    for _, replica_name in ipairs(cg.non_synced_replicas) do
        cg[replica_name]:wait_until_ready()
    end

    -- Wait for replica1 to connect to replica2, replica3 before connecting
    -- to the bootstrap master to ensure that replica2, replica3
    -- is counted in the sync quorum
    require('fiber').sleep(1)

    cg.master_proxy:resume()

    -- Wait until replica1 get bootstraped
    cg.replica1:wait_until_ready()
    -- Replica1 cannot synchronize with replica3,
    -- so it goes into an 'orphan' status
    cg.replica1:exec(function()
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.status, 'orphan')
        end)
    end)

    -- Shutdown
    for _, replica_name in ipairs(cg.non_synced_replicas) do
        cg[replica_name]:exec(function()
            box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', false)
        end)
    end
end
