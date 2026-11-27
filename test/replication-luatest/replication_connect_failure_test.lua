local t = require('luatest')
local server = require('luatest.server')

local g = t.group('replication_connect_failure', {{with_self = false},
                                                {with_self = true}})

g.before_each(function(cg)
    cg.bad = server:new({alias = 'bad'})
    cg.bad:start()
    cg.bad:exec(function()
        box.iproto.override(box.iproto.type.WATCH, function(_, body)
            if body[box.iproto.key.EVENT_KEY] == 'internal.ballot' then
                box.error(box.error.PROTOCOL, 'test ballot error')
            end
            return false
        end)
    end)
    cg.server = server:new({
        alias = 'server',
        box_cfg = {replication_connect_timeout = 1000},
    })
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
    cg.bad:drop()
end)

g.test_stopped_applier_does_not_delay_reconfiguration = function(cg)
    local uris = {cg.bad.net_box_uri}
    if cg.params.with_self then
        table.insert(uris, cg.server.net_box_uri)
    end
    cg.server:exec(function(uris)
        local fiber = require('fiber')
        local f = fiber.new(function()
            box.cfg{replication = uris}
        end)
        f:set_joinable(true)
        -- A permanent error leaves no connection attempts to wait for.
        t.assert(f:join(120))
        t.assert_not(box.info.ro)
        box.cfg{replication = {}}
    end, {uris})
    t.assert(cg.server:grep_log('test ballot error'))
end
