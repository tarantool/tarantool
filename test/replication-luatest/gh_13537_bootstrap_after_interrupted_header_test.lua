local t = require('luatest')
local server = require('luatest.server')
local proxy = require('luatest.replica_proxy')
local msgpack = require('msgpack')

local g = t.group()

g.before_all(function(cg)
    cg.master = server:new({alias = 'master'})
    cg.master:start()
end)

g.after_all(function(cg)
    if cg.replica then
        cg.replica:drop()
    end
    if cg.replica_proxy then
        cg.replica_proxy:stop()
    end
    cg.master:drop()
end)

g.test_bootstrap_after_interrupted_header = function(cg)
    local interrupted = false
    cg.replica_proxy = proxy:new({
        client_socket_path = server.build_listen_uri('master_proxy'),
        server_socket_path = cg.master.net_box_uri,
        process_server = {
            -- Read the greeting and then whole IPROTO packets to cut the
            -- first JOIN exactly after the OK with VCLOCK,
            -- before any metadata or snapshot data reaches the replica.
            -- After that, func forwards traffic unchanged so a retry can
            -- complete.
            pre = function(conn)
                if interrupted then
                    return
                end
                local sock = conn.server_socket
                conn:forward_to_client(sock:read(box.iproto.GREETING_SIZE, 120))
                while true do
                    local prefix = sock:read(5, 120)
                    if prefix == nil or #prefix == 0 then
                        return
                    end
                    local data = sock:read(msgpack.decode(prefix), 120)
                    local packet = prefix .. data
                    local header, body = box.iproto.decode_packet(packet)
                    conn:forward_to_client(packet)
                    if interrupted then
                        return
                    end
                    local request_type = header[box.iproto.key.REQUEST_TYPE]
                    local vclock = body[box.iproto.key.VCLOCK]
                    if request_type == box.iproto.type.OK and vclock ~= nil then
                        interrupted = true
                        -- Older masters can send an error after the header
                        -- without sending any join metadata first.
                        header = header:decode()
                        header[box.iproto.key.REQUEST_TYPE] =
                            box.iproto.type.TYPE_ERROR + box.error.SYSTEM
                        body = {[box.iproto.key.ERROR_24] = 'Snapshot error'}
                        conn:forward_to_client(box.iproto.encode_packet(
                            header, body))
                        conn:stop()
                        return
                    end
                end
            end,
            func = function(conn, data) conn:forward_to_client(data) end,
            post = function(conn) conn:stop() end,
        },
    })
    t.assert(cg.replica_proxy:start({force = true}))
    cg.replica = server:new({
        alias = 'replica',
        box_cfg = {
            replication = {cg.replica_proxy.client_socket_path},
            replication_connect_timeout = 120,
            replication_reconnect_timeout = 0.01,
        },
    })
    cg.replica:start()
    t.assert(interrupted)
end
