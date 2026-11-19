local t = require('luatest')
local server = require('luatest.server')
local fio = require('fio')

local g = t.group('ballot_error')

g.before_each(function(cg)
    cg.bad = server:new({alias = 'bad'})
    cg.bad:start()
    cg.bad:exec(function()
        box.iproto.override(box.iproto.type.ID, function()
            box.error(box.error.UNKNOWN_REQUEST_TYPE, box.iproto.type.ID)
        end)
        box.iproto.override(box.iproto.type.VOTE, function(header)
            -- A VOTE reply without a body fails after initializing the
            -- ballot's default is_booted value to true.
            box.iproto.send(box.session.id(), {
                [box.iproto.key.REQUEST_TYPE] = box.iproto.type.OK,
                [box.iproto.key.SYNC] = header[box.iproto.key.SYNC],
            })
            return true
        end)
    end)
    cg.server = server:new({
        alias = 'server',
        box_cfg = {replication_connect_timeout = 1000},
    })
    cg.server.box_cfg.replication = {cg.server.net_box_uri, cg.bad.net_box_uri}
end)

g.after_each(function(cg)
    cg.server:drop()
    cg.bad:drop()
end)

g.test_failed_ballot_does_not_lower_bootstrap_quorum = function(cg)
    cg.server:start({wait_until_ready = false})
    t.helpers.retrying({timeout = 120}, function()
        t.assert(not cg.server.process:is_alive() or
                 cg.server:grep_log('ready to accept requests'))
    end)
    -- Only the connection to self succeeded. An invalid ballot must not
    -- change bootstrap to join and lower the required quorum from 2 to 1.
    t.assert_not(cg.server.process:is_alive())
    t.assert(cg.server:grep_log('INVALID_MSGPACK'))
    t.assert(cg.server:grep_log('failed to connect to one or more replicas'))
    t.assert_equals(fio.glob(fio.pathjoin(cg.server.workdir, '*.snap')), {})
end
