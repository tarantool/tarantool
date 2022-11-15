local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local proxy = require('luatest.replica_proxy')

local fio = require('fio')
local fiber = require('fiber')

local g = t.group('bootstrap-connection-failure')

local timeout = 0.1

g.before_all(function(cg)
    cg.cluster = cluster:new{}
    cg.servers = {}
    local box_cfg = {
        replication = {
            server.build_listen_uri('server_1'),
            server.build_listen_uri('server_2'),
            server.build_listen_uri('server_3'),
        },
        replication_timeout = timeout,
        bootstrap_strategy = 'legacy',
    }
    cg.uuids = {
        'bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb',
        'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa',
        'cccccccc-cccc-cccc-cccc-cccccccccccc',
    }

    -- Connection server_3 -> server_1 is proxied, others are not.
    cg.proxy = proxy:new{
        client_socket_path = server.build_listen_uri('server_1_proxy'),
        server_socket_path = server.build_listen_uri('server_1'),
    }
    t.assert(cg.proxy:start{force = true}, 'Proxy started successfully')
    for i = 1, 3 do
        box_cfg.instance_uuid = cg.uuids[i]
        if i == 3 then
            box_cfg.replication[1] = server.build_listen_uri('server_1_proxy')
        end
        cg.servers[i] = cg.cluster:build_and_add_server({
            alias = 'server_' .. i,
            box_cfg = box_cfg,
        })
    end
end)

g.after_all(function(cg)
    cg.cluster:drop()
end)

local bootstrap_msg = 'bootstrapping replica from '
local ready_msg = 'ready to accept requests'

local function wait_bootstrapped(server, uuidstr)
    local logfile = fio.pathjoin(server.workdir, server.alias .. '.log')
    -- Prepare the search pattern for grep log.
    local query = string.gsub(uuidstr, '%-', '%%-')
    t.helpers.retrying({}, function()
        t.assert(server:grep_log(bootstrap_msg .. query, nil,
                                 {filename = logfile}),
                 'Server ' .. server.alias .. ' chose ' .. uuidstr ..
                 ' as bootstrap leader')
        t.assert(server:grep_log(ready_msg, nil, {filename = logfile}),
                 'Server ' .. server.alias .. ' is bootstrapped')
    end)
end

--
-- Make sure there is no deadlock when bootstrapping 3 nodes with connectivity
-- problems.
-- The issue might appear when some node manages to connect only part of the
-- cluster before bootstrap happens. Once the node connects to everyone, the
-- remote ballots it has fetched will be inconsistent to choose a bootstrap
-- leader. The real leader might still have ballot.is_booted = false in its old
-- ballot, while the node might choose someone with ballot.is_booted = true as
-- its bootstrap leader.
-- So the node has to re-fetch everyone's ballots in order to choose the same
-- bootstrap leader as everyone else.
g.test_bootstrap_with_bad_connection = function(cg)
    cg.proxy:pause()
    cg.cluster:start{wait_until_ready = false}
    wait_bootstrapped(cg.servers[1], cg.uuids[2])
    fiber.sleep(timeout)
    local logfile = fio.pathjoin(cg.servers[3].workdir, 'server_3.log')
    t.assert(not cg.servers[3]:grep_log(bootstrap_msg, nil,
                                        {filename = logfile}),
             'Server 3 waits for connection')
    cg.proxy:resume()
    wait_bootstrapped(cg.servers[3], cg.uuids[2])
end
