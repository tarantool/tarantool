local replica_client = require('test.replication-luatest.lib.replica_client')
local replica_set = require('luatest.replica_set')
local t = require('luatest')

local g = t.group()

local TIMEOUT = 60

local key = box.iproto.key

--
-- One real node, shared by the tests. The replicas registered by a test are
-- forgotten by the node after it.
--
g.before_all(function(cg)
    cg.replica_set = replica_set:new{}
    cg.node1 = cg.replica_set:build_and_add_server{alias = 'node1'}
    cg.replica_set:start()
    cg.node1:exec(function()
        box.schema.space.create('test'):create_index('pk')
        box.space.test:insert{1}
    end)
end)

g.after_each(function(cg)
    cg.node1:exec(function()
        for _, tuple in box.space._cluster:pairs() do
            if tuple[1] ~= box.info.id then
                box.space._cluster:delete{tuple[1]}
            end
        end
    end)
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

--
-- The join of a real node, the way a replica does it: a snapshot is fetched,
-- then the replica registers.
--
g.test_join = function(cg)
    local node1_uri = cg.node1.net_box_uri
    local space_id = cg.node1:exec(function()
        return box.space.test.id
    end)
    local node1_vclock = cg.node1:get_vclock()
    node1_vclock[0] = nil

    local snapshot = replica_client.fetch_snapshot(node1_uri, {
        timeout = TIMEOUT,
    })
    t.assert_not_equals(snapshot.uuid, nil)
    -- The header of the snapshot has the whole vclock, with the component of
    -- the local rows. The node writes such rows for the joining replica, so
    -- only the rest is known in advance. The end markers go without it.
    local start_vclock = table.copy(snapshot.start_vclock)
    t.assert_not_equals(start_vclock[0], nil)
    start_vclock[0] = nil
    t.assert_equals(start_vclock, node1_vclock)
    t.assert_equals(snapshot.vclock, node1_vclock)
    -- The join meta has the Raft state.
    local has_raft = false
    for _, packet in ipairs(snapshot.meta) do
        if packet.header[key.REQUEST_TYPE] == box.iproto.type.RAFT then
            has_raft = true
            break
        end
    end
    t.assert(has_raft)
    -- The data rows have the tuple of the user space.
    local tuples = {}
    for _, packet in ipairs(snapshot.rows) do
        if packet.body[key.SPACE_ID] == space_id then
            table.insert(tuples, packet.body[key.TUPLE])
        end
    end
    t.assert_equals(tuples, {{1}})

    local registration = replica_client.register(node1_uri, {
        uuid = snapshot.uuid,
        vclock = snapshot.vclock,
        timeout = TIMEOUT,
    })
    t.assert_equals(registration.uuid, snapshot.uuid)
    t.assert_not_equals(registration.instance_id, nil)
    cg.node1:exec(function(id, uuid)
        t.assert_equals(box.space._cluster:get{id}, {id, uuid})
    end, {registration.instance_id, registration.uuid})
    -- The registration is a row of the node, and the replica has got it.
    node1_vclock = cg.node1:get_vclock()
    node1_vclock[0] = nil
    t.assert_equals(registration.vclock, node1_vclock)
end
