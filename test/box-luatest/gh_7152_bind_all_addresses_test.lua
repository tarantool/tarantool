local t = require("luatest")
local server = require('luatest.server')

local g = t.group()

g.before_all(function(g)
    local socket = require('socket')
    local fiber = require('fiber')
    local port
    local port_found = false
    while not port_found do
        local sock = socket('AF_INET', 'SOCK_STREAM', 'tcp')
        -- It is one of the rare cases when we can't just use 0 to bind
        -- some free port since we are testing that it is possible to
        -- bind several interfaces to the same port. So we need to provide
        -- some additional efforts to find the port number which is:
        -- 1. Not used by another tests;
        -- 2. Not used by possible another instances of the test.
        port = math.random(3350, 4000)
        if sock:bind("127.0.0.1", port) then
            port_found = true
            -- The delay here is intended to increase the probability of
            -- other instances of the test to fail to bind the port so
            -- they would find another port.
            fiber.sleep(1)
        end
        sock:close()
    end
    g.server = server:new({box_cfg={log_level = 7}, net_box_port = port})
    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
end)

g.test_bind_single_port_all_interfaces = function(g)
    g.server:exec(function()
        local socket = require('socket')
        local net_box = require('net.box')
        local uri = require('uri')
        local log = require('log')
        local port
        if type(box.info.listen) == 'string' then
            port = uri.parse(box.info.listen).service
        else
            -- The port is assumed to be the same for all listen entries.
            port = uri.parse(box.info.listen[1]).service
        end
        local addrinfo = socket.getaddrinfo(nil, port, 1, {protocol = 'tcp'})
        log.debug(addrinfo)
        t.skip_if(#addrinfo <= 1)
        for _, item in ipairs(addrinfo) do
            local netbox_uri
            if item.family == "AF_INET6" then
                netbox_uri = string.format("[%s]:%d", item.host, item.port)
            else
                netbox_uri = string.format("%s:%d", item.host, item.port)
            end
            log.info("connecting %s", netbox_uri)
            local conn = net_box.connect(netbox_uri)
            local rc = conn:ping()
            conn:close()
            t.assert(rc)
        end
    end)
end
