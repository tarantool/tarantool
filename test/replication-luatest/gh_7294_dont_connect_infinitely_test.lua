local luatest = require('luatest')
local server = require('luatest.server')
local fio = require('fio')

local g = luatest.group('gh_7294_infinite_connection_to_wrong_ip')

g.before_each(function(cg)
    cg.server = server:new({
        alias = 'server',
        box_cfg = {
            replication = {
                -- An address from TEST-NET-1, as described in RFC 5735
                -- (https://www.rfc-editor.org/rfc/rfc5735).
                '192.0.2.0:3301',
            },
            replication_connect_timeout = 1000,
            replication_timeout = 0.1,
        },
    })
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_no_infinite_connection = function(cg)
    cg.server:start({wait_until_ready = false})

    luatest.helpers.retrying({}, function()
        -- Pass log filepath manually, because box.cfg.log is not available.
        local log = fio.pathjoin(cg.server.workdir, cg.server.alias .. '.log')
        luatest.assert(cg.server:grep_log('applier/192.0.2.0:3301 .* TimedOut',
                                          1024, {filename = log}),
                       'Timeout happened')
    end)
end
