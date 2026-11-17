local replica_client = require('test.replication-luatest.lib.replica_client')
local replica_server = require('test.replication-luatest.lib.replica_server')
local replica_set = require('luatest.replica_set')
local t = require('luatest')

local g = t.group()

local TIMEOUT = 60

local key = box.iproto.key

--
-- One real node, shared by the tests. Each test gets a server of its own, with
-- a newly registered identity: the node remembers the rows it got from the
-- previous ones.
--
g.before_all(function(cg)
    cg.replica_set = replica_set:new{}
    cg.node1 = cg.replica_set:build_and_add_server{
        alias = 'node1',
        box_cfg = {
            -- The server sends no heartbeats on its own.
            replication_timeout = 1000,
            replication_reconnect_timeout = 0.1,
        },
    }
    cg.replica_set:start()
    cg.space_id = cg.node1:exec(function()
        local space = box.schema.space.create('test')
        space:create_index('pk')
        return space.id
    end)
end)

g.before_each(function(cg)
    local node1_uri = cg.node1.net_box_uri
    local snapshot = replica_client.fetch_snapshot(node1_uri, {
        timeout = TIMEOUT,
    })
    local registration = replica_client.register(node1_uri, {
        uuid = snapshot.uuid,
        vclock = snapshot.vclock,
        timeout = TIMEOUT,
    })
    cg.server = replica_server.new{
        alias = 'server',
        replica_set_id = cg.replica_set.id,
        uuid = registration.uuid,
        instance_id = registration.instance_id,
        timeout = TIMEOUT,
    }
    cg.server:start()
end)

g.after_each(function(cg)
    cg.node1:update_box_cfg{replication = {}}
    cg.server:stop()
    cg.node1:exec(function()
        box.space.test:truncate()
    end)
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

--
-- A real node subscribes to the server, follows it, applies a row sent by it,
-- and acks the row.
--
g.test_subscribe_and_send = function(cg)
    local server = cg.server
    cg.node1:update_box_cfg{replication = {server.uri}}
    local session = server:wait_subscribe()
    t.assert_equals(session.subscribe.body[key.INSTANCE_UUID],
                    cg.node1:get_instance_uuid())
    t.helpers.retrying({timeout = TIMEOUT}, function()
        cg.node1:assert_follows_upstream(server.instance_id)
    end)

    session:send(replica_server.rows.insert{
        origin = server.instance_id,
        lsn = 1,
        space_id = cg.space_id,
        tuple = {1},
    })
    cg.node1:exec(function(timeout)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.space.test:get{1}, {1})
        end)
    end, {TIMEOUT})
    t.helpers.retrying({timeout = TIMEOUT}, function()
        t.assert_not_equals(session.ack_vclock, nil)
        t.assert_equals(session.ack_vclock[server.instance_id], 1)
    end)
end

--
-- A closed session is a lost upstream for the node. It reconnects, and the
-- new subscription is the next session of the server.
--
g.test_session_close = function(cg)
    local server = cg.server
    cg.node1:update_box_cfg{replication = {server.uri}}
    local session1 = server:wait_subscribe()
    session1:close()
    local session2 = server:wait_subscribe()
    t.assert_not_equals(session2, session1)
    t.helpers.retrying({timeout = TIMEOUT}, function()
        cg.node1:assert_follows_upstream(server.instance_id)
    end)

    session2:send(replica_server.rows.insert{
        origin = server.instance_id,
        lsn = 1,
        space_id = cg.space_id,
        tuple = {1},
    })
    cg.node1:exec(function(timeout)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.space.test:get{1}, {1})
        end)
    end, {TIMEOUT})
end

--
-- A stopped server is gone for the node: no sessions, nothing to connect to.
-- Started again, it is found by the node, and the subscribers are counted
-- anew.
--
g.test_stop_and_start = function(cg)
    local server = cg.server
    cg.node1:update_box_cfg{replication = {server.uri}}
    server:wait_subscribe()
    t.helpers.retrying({timeout = TIMEOUT}, function()
        cg.node1:assert_follows_upstream(server.instance_id)
    end)

    server:stop()
    cg.node1:exec(function(id, timeout)
        t.helpers.retrying({timeout = timeout}, function()
            local upstream = box.info.replication[id].upstream
            t.assert_not_equals(upstream.status, 'follow')
        end)
    end, {server.instance_id, TIMEOUT})
    t.assert_equals(server.subscribers, {})

    server:start()
    server:wait_subscribe()
    t.assert_equals(#server.subscribers, 1)
    t.helpers.retrying({timeout = TIMEOUT}, function()
        cg.node1:assert_follows_upstream(server.instance_id)
    end)
end

--
-- The server sends what it is told to, a row a real upstream would never send
-- too. The node refuses such a row and stops the replication.
--
g.test_bad_row = function(cg)
    local server = cg.server
    cg.node1:update_box_cfg{replication = {server.uri}}
    local session = server:wait_subscribe()
    -- No such space.
    session:send(replica_server.rows.insert{
        origin = server.instance_id,
        lsn = 1,
        space_id = cg.space_id + 1,
        tuple = {1},
    })
    cg.node1:exec(function(id, timeout)
        t.helpers.retrying({timeout = timeout}, function()
            local upstream = box.info.replication[id].upstream
            t.assert_equals(upstream.status, 'stopped')
            t.assert_str_contains(upstream.message, 'does not exist')
        end)
        t.assert_equals(box.info.vclock[id], nil)
    end, {server.instance_id, TIMEOUT})
end
