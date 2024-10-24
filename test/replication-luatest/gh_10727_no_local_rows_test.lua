local t = require('luatest')
local server = require('luatest.server')
local proxy = require('luatest.replica_proxy')
local os = require('os')

local g = t.group()

g.before_all(function(cg)
    cg.master = server:new{alias = 'master'}
    cg.master:start()
    cg.master:exec(function()
        box.schema.space.create('test', {is_local = true}):create_index('pk')
        box.space.test:insert{1, 'data'}
    end)
end)

g.after_all(function(cg)
    cg.master:stop()
end)

local function process_replication_flow(conn, data)
    local ok, h = pcall(box.iproto.decode_packet, data)
    if ok and h.group_id == 1 then
        -- This is the only way, I managed to find to stop the test, when
        -- wrong result is found. Everything is pcalled there.
        os.exit()
    end

    conn:forward_to_client(data)
end

g.test_no_local_rows_are_sent = function(cg)
    local proxy_uri = server.build_listen_uri('master_proxy')
    local replica = server:new{alias = 'replica', box_cfg = {
        replication = { proxy_uri },
    }}
    local server_proxy = proxy:new({
        client_socket_path = proxy_uri,
        server_socket_path = cg.master.net_box_uri,
        process_server = {
            func = process_replication_flow,
        },
    })

    server_proxy:start({force = true})
    replica:start()
    replica:wait_for_vclock_of(cg.master)
    cg.master:exec(function() box.ctl.promote() end)
    replica:wait_for_vclock_of(cg.master)

    replica:stop()
    server_proxy:stop()
end
