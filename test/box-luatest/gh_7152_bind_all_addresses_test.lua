local t = require("luatest")
local server = require('luatest.server')

local g = t.group()

g.before_all(function(g)
    local port = 3350
    local socket = require('socket')
    local sock_v4 = socket('AF_INET', 'SOCK_STREAM', 'tcp')
    local ipv4_bind = sock_v4:bind("0.0.0.0", port) and sock_v4:listen(5)
    sock_v4:close()
    local sock_v6 = socket('AF_INET6', 'SOCK_STREAM', 'tcp')
    local ipv6_bind = sock_v6:bind("::", port) and sock_v6:listen(5)
    sock_v6:close()
    t.run_only_if(ipv4_bind and ipv6_bind,
                  "not in a dualstack configuration (listen)")
    local addrinfo = socket.getaddrinfo(
        "::", port,
        {
            family = "AF_INET6",
            type = "SOCK_STREAM",
            flags = {"AI_PASSIVE", "AI_ADDRCONFIG"}
        }
    )
    local ipv6_addrinfo = false
    if addrinfo ~= nil then
        for _, entry in pairs(addrinfo) do
            if entry.family == "AF_INET6" then
                ipv6_addrinfo = true
            end
        end
    end
    t.run_only_if(ipv6_addrinfo,
                  "not in a dualstack configuration (getaddrinfo)")
    g.server = server:new({net_box_port = port})
    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
end)

g.test_bind_single_port_all_interfaces = function(g)
    g.server:exec(function()
        local port = 3350
        local net_box = require('net.box')
        t.assert_equals(type(box.info.listen), 'table')
        t.assert_equals(#box.info.listen, 2)
        local conn_v4 = net_box.connect(string.format("127.0.0.1:%d", port))
        local rc = conn_v4:ping()
        conn_v4:close()
        t.assert(rc)
        local conn_v6 = net_box.connect(string.format("[::1]:%d", port))
        local rc = conn_v6:ping()
        conn_v6:close()
        t.assert(rc)
    end)
end
