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
        return box.info.replicaset and
            box.info.replicaset.uuid or box.info.cluster.uuid
    end)

    local encoded_packet = box.iproto.encode_packet(
        {[box.iproto.key.SYNC] = g.conn:_next_sync(),
         [box.iproto.key.REQUEST_TYPE] = box.iproto.type.SUBSCRIBE},
        {[box.iproto.key.REPLICASET_UUID] = replicaset_id,
         [box.iproto.key.REPLICA_ANON] = true})

    local res, err = pcall(g.conn._inject, g.conn, encoded_packet)
    t.assert_not(res)
    t.assert_equals(err.code, box.error.PROTOCOL)
    t.assert_equals("Can't subscribe a replica which doesn't exist",
                    err.message)
end
