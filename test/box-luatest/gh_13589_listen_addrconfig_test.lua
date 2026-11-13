local t = require('luatest')
local server = require('luatest.server')
local socket = require('socket')

local g = t.group('gh-13589', {{host = '127.0.0.1'}, {host = '[::1]'}})

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    if cg.params.host == '[::1]' then
        local s = socket('AF_INET6', 'SOCK_STREAM', 'tcp')
        local ok = s ~= nil and s:bind('::1', 0)
        if s ~= nil then
            s:close()
        end
        t.skip_if(not ok, 'IPv6 is unavailable')
    end
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

-- Listen must work when getaddrinfo() with AI_ADDRCONFIG fails with
-- EAI_ADDRFAMILY, like glibc does for 127.0.0.1 on a host whose only
-- non-loopback addresses are IPv6, or for ::1 on a host without
-- non-loopback IPv6 addresses.
g.test_listen_addrconfig_retry = function(cg)
    cg.server:exec(function(host)
        local net_box = require('net.box')
        local inj = 'ERRINJ_SIO_GETADDRINFO_ADDRCONFIG'
        box.error.injection.set(inj, true)
        box.cfg{listen = host .. ':0'}
        -- The injection turns itself off once hit.
        t.assert_not(box.error.injection.get(inj))
        local conn = net_box.connect(box.info.listen)
        t.assert(conn:ping())
        conn:close()
    end, {cg.params.host})
end
