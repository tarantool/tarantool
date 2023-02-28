local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

g.before_all = function(lg)
    lg.master = server:new({alias = 'master'})
    lg.master:start()
end

g.after_all = function(lg)
    lg.master:drop()
end

--
-- When an instance failed to apply cfg{replication_anon = false}, it used to
-- report itself as non-anon in the ballot anyway. Shouldn't be so.
--
g.test_ballot_on_deanon_fail = function(lg)
    local box_cfg = {
        replication_anon = true,
        read_only = true,
        replication = {
            lg.master.net_box_uri,
        },
    }
    local replica = server:new({
        alias = 'replica',
        box_cfg = box_cfg,
    })
    replica:start()
    replica:exec(function()
        t.assert_equals(box.info.id, 0)
    end)
    lg.master:exec(function()
        rawset(_G, 'test_trigger', function()
            error('Reject _cluster update')
        end)
        box.space._cluster:on_replace(_G.test_trigger)
    end)
    replica:exec(function()
        local fiber = require('fiber')
        local iproto = box.iproto
        local is_anon = nil
        local w = box.watch('internal.ballot', function(_, value)
            is_anon = value[iproto.key.BALLOT][iproto.ballot_key.IS_ANON]
        end)
        fiber.yield()
        t.assert(is_anon)
        is_anon = nil
        t.assert_error_msg_contains('Reject _cluster update', box.cfg,
                                    {replication_anon = false})
        fiber.yield()
        t.assert(is_anon)
        w:unregister()
    end)
    replica:drop()
    lg.master:exec(function()
        box.space._cluster:on_replace(nil, _G.test_trigger)
        _G.test_trigger = nil
    end)
end
