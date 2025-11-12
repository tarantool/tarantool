local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

-- Check if IPv6 is supported on the test machine.
local function ipv6_supported()
    local socket = require('socket')
    local sock = socket('AF_INET6', 'SOCK_STREAM', 'tcp')
    local bind_ok = sock:bind("::", 0) and sock:listen(5)
    sock:close()
    local addrinfo = socket.getaddrinfo("::", 0, {
        family = "AF_INET6",
        type = "SOCK_STREAM",
        flags = {"AI_PASSIVE", "AI_ADDRCONFIG"}
    })
    local getaddrinfo_ok = false
    if addrinfo ~= nil then
        for _, entry in pairs(addrinfo) do
            if entry.family == "AF_INET6" then
                getaddrinfo_ok = true
            end
        end
    end
    return bind_ok and getaddrinfo_ok
end

g.test_replication = function()
    -- Successfully set up replication using the given interface.
    local function check_ok(listen, ifname)
        local srv = server:new({alias = 'master'})
        srv:start()
        srv:exec(function(listen, ifname)
            box.cfg({listen = {box.cfg.listen, listen}})
            box.cfg({replication = {box.info().listen[1],
                                    params = {interface = ifname}}})
        end, {listen, ifname})
        t.assert(srv:grep_log('IllegalParams') == nil)
        srv:drop()
    end

    -- Fail to connect using the given interface and expect an error.
    local function check_fail(listen, ifname, expected_error)
        local srv = server:new({alias = 'master'})
        srv:start()
        srv:exec(function(listen, ifname)
            box.cfg({listen = {box.cfg.listen, listen}})
            -- Use the URI from box.cfg in case of a UNIX socket,
            -- use box.info() othervise to get the port number.
            local uri = box.info().listen[1]:find("^unix/:") and
                        box.cfg.listen[2] or box.info().listen[1]
            box.cfg({replication = {uri, params = {interface = ifname}},
                     replication_connect_timeout = 0.1})
        end, {listen, ifname})
        t.assert(srv:grep_log(expected_error) ~= nil)
        srv:drop()
    end

    -- Listen hostname.
    check_ok('localhost:0', 'lo')
    check_fail('localhost:0', 'lol', 'suitable interface not found: lol')

    -- Listen IPv4.
    check_ok('127.0.0.1:0', 'lo')
    check_fail('127.0.0.1:0', 'lol', 'suitable interface not found: lol')

    -- Listen IPv6.
    if ipv6_supported() then
        check_ok('[::1]:0', 'lo')
        check_fail('[::1]:0', 'lol', 'suitable interface not found: lol')
    end

    -- Listen UNIX socket: /path/to.sock.
    local fio = require('fio')
    local SOCKET_PATH = fio.pathjoin(server.vardir, 'gh-11803-replication.sock')
    local unix_error_msg = 'interface is specified for non%-IP connection'
    check_fail(SOCKET_PATH, 'lo', unix_error_msg)
    check_fail(SOCKET_PATH, 'lol', unix_error_msg)

    -- Listen UNIX socket with protocol: unix/:/path/to.sock.
    check_fail('unix/:' .. SOCKET_PATH, 'lo', unix_error_msg)
    check_fail('unix/:' .. SOCKET_PATH, 'lol', unix_error_msg)
end
