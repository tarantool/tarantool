local server = require('luatest.server')
local t = require('luatest')
local urilib = require('uri')

local g = t.group('before_replace alter',
    t.helpers.matrix{ret_nil = {true, false}})

g.test_before_replace_alter_replica_id = function(cg)
    local ret_nil = cg.params.ret_nil
    local server1 = server:new{
        alias = 'server1',
    }
    server1:start()
    server1:exec(function(ret_nil)
        local trigger
        if ret_nil == false then
            trigger = function(_old, new)
                return new:update({{'+', 1, 10}})
            end
        else
            trigger = function(_old, _new)
                return nil
            end
        end
        box.space._cluster:before_replace(trigger)
    end, {ret_nil})
    local uri = urilib.parse(server1.net_box_uri)
    local server2 = server:new{
        alias = 'server2',
        box_cfg = {replication = uri.unix}
    }

    if ret_nil == false then
        server2:start()
        local msg = server1:grep_log('Replica ID is changed by a trigger', 1024)
        t.assert_not_equals(msg, nil)
    else
        -- We cannot wait until ready because server2 will not be started at all,
        -- so we will retry until we find a log entry we need.
        server2:start({wait_until_ready=false})
        t.helpers.retrying({}, function()
            local msg = server1:grep_log('Replica ID is changed by a trigger', 1024)
            if msg == nil then
                error('Cannot find replica connection error')
            end
        end)
    end

    server2:drop()
    server1:drop()
end
