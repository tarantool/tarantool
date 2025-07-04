local t = require("luatest")
local server = require("luatest.server")
local net_box = require("net.box")

local g = t.group()

g.before_all(function()
    g.master = server:new({ alias = "master" })
    g.master:start()
    g.conn = net_box.connect(g.master.net_box_uri)
end)

g.after_all(function()
    g.master:drop()
    g.conn:close()
end)

g.test_no_crash_with_anon_subscribe_request_and_nil_instance_uuid = function()
    local replicaset_id = g.master:exec(function()
        return box.info.replicaset.uuid
    end)

    local encoded_packet = box.iproto.encode_packet(
        {[box.iproto.key.SYNC] = g.conn:_next_sync(),
         [box.iproto.key.REQUEST_TYPE] = box.iproto.type.SUBSCRIBE},
        {[box.iproto.key.REPLICASET_UUID] = replicaset_id,
         [box.iproto.key.REPLICA_ANON] = true})

    -- There can be a rare situation when our subscribe request can't be
    -- processed due to some pending requests in our connection. These pending
    -- requests don't have enough time to be deleted and, as a result the
    -- ER_PROTOCOL error is raised. We shouldn't fail our test due to this
    -- error.
    t.helpers.retrying({delay = 0.1}, function()
        -- If the ER_PROTOCOL error is raised, the connection is closed.
        -- In this scenario our test will fail due to ER_NO_CONNECTION error.
        -- On each iteration of t.helpers.retrying we should check that our
        -- connection is alive.
        if not g.conn:is_connected() then
            g.conn = net_box.connect(g.master.net_box_uri)
            g.conn:wait_connected()
        end
        local res, err = pcall(g.conn._inject, g.conn, encoded_packet)
        -- If some kind of crashes (e.g. segmentation fault) appears in
        -- g.conn._inject, the result of pcall will equal to true. It means
        -- that if we fail on another assertion we can lose our crash info.
        -- In this case if we get a crash inside g.conn._inject, it will
        -- be printed in console, otherwise we will check error's code.
        if not res then
            t.assert_equals(err.code, box.error.NIL_UUID)
        end
    end)
end
