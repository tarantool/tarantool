local luatest = require('luatest')
local server = require('luatest.server')
local proxy = require('luatest.replica_proxy')
local fio = require('fio')

local g = luatest.group('gh_8185_nil_uuid_connection')

g.before_each(function(cg)
    cg.server = server:new({
        alias = 'tnt_server',
        box_cfg = {
            replication = {
                server.build_listen_uri('proxy'),
            },
        },
    })

    cg.proxy = proxy:new({
        client_socket_path = cg.server.box_cfg.replication[1],
        server_socket_path = "/dev/null",

        -- Proxy will send nil UUID greeting as soon as client connects.
        process_client = {
            pre = function(c)
                c:forward_to_client(
                    'Tarantool 2.11.0 (Binary) '..
                    '00000000-0000-0000-0000-000000000000 \n'..
                    'y8PniqYLPVESGsAYwA+1Mm4NphVCVgDE3zBGpdiI5/c='..
                    '                   \n')
                c:stop()
            end,
        },
    })
    cg.proxy:start({force = true})
end)

g.after_each(function(cg)
    cg.proxy:stop()
    cg.server:drop()
end)

g.test_nil_uuid = function(cg)
    cg.server:start({wait_until_ready = false})

    luatest.helpers.retrying({}, function()
        -- Pass log filepath manually, because box.cfg.log is not available.
        local log = fio.pathjoin(cg.server.workdir, cg.server.alias .. '.log')
        luatest.assert(cg.server:grep_log('ER_NIL_UUID', 1024, {
            filename = log}), 'Error detected')
    end)
end
