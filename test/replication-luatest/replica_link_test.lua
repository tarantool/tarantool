local replica_link = require('test.replication-luatest.lib.replica_link')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

local TIMEOUT = 60

local is_insert = replica_link.is_type(box.iproto.type.INSERT)

-- The positions of the INSERT packets in the queue.
local function find_inserts(queue)
    local result = {}
    for i, packet in ipairs(queue) do
        if is_insert(packet) then
            table.insert(result, i)
        end
    end
    return result
end

--
-- Node 2 replicates from node 1 through a link. Node 1 has no upstream. The
-- replica set is shared by the tests: each one leaves the link working, and
-- the space is cleared after it.
--
g.before_all(function(cg)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication_timeout = 0.1,
    }
    cg.node1 = cg.replica_set:build_and_add_server{
        alias = 'node1',
        box_cfg = box_cfg,
    }
    box_cfg.replication = {
        server.build_listen_uri('node1', cg.replica_set.id),
    }
    cg.node2 = cg.replica_set:build_and_add_server{
        alias = 'node2',
        box_cfg = box_cfg,
    }
    cg.replica_set:start()
    cg.node1:exec(function()
        box.schema.space.create('test'):create_index('pk')
    end)
    cg.node1:wait_for_downstream_to(cg.node2)

    cg.link = replica_link.new{
        upstream = cg.node1.net_box_uri,
        alias = 'link_2_from_1',
        replica_set_id = cg.replica_set.id,
        timeout = TIMEOUT,
    }
    cg.link:start()
    -- The tests stop the link's packets for a while, the heartbeats among
    -- them. That silence must not break the connection, so the replication
    -- timeout is made huge. It is raised on both ends with the replication
    -- closed: a read in flight keeps the timeout it has started with.
    cg.node2:update_box_cfg{replication = {}}
    for _, node in pairs({cg.node1, cg.node2}) do
        node:update_box_cfg{
            replication_timeout = 1000,
            replication_reconnect_timeout = box_cfg.replication_timeout,
        }
    end
    cg.node2:update_box_cfg{replication = {cg.link.uri}}
    local node1_id = cg.node1:get_instance_id()
    t.helpers.retrying({timeout = TIMEOUT}, function()
        cg.node2:assert_follows_upstream(node1_id)
    end)
    cg.node1:wait_for_downstream_to(cg.node2)
end)

g.after_each(function(cg)
    -- A failed test might have left a direction held.
    for _, direction in pairs({cg.link.down, cg.link.up}) do
        if direction.is_holding then
            direction:release_flush()
        end
    end
    cg.node1:exec(function()
        box.space.test:truncate()
    end)
    cg.node1:wait_for_downstream_to(cg.node2)
end)

g.after_all(function(cg)
    if cg.link ~= nil then
        cg.link:stop()
    end
    cg.replica_set:drop()
end)

--
-- A held row waits in the queue and doesn't reach the downstream until the
-- release.
--
g.test_hold_and_release = function(cg)
    local link = cg.link
    link.down:hold()
    cg.node1:exec(function()
        box.space.test:insert{1}
    end)
    link.down:wait_held(function(queue)
        return replica_link.has_type(queue, box.iproto.type.INSERT)
    end)
    cg.node2:exec(function()
        t.assert_equals(box.space.test:get{1}, nil)
    end)
    link.down:release_flush()
    t.assert_equals(link.down.queue, {})
    cg.node2:wait_for_vclock_of(cg.node1)
    cg.node2:exec(function()
        t.assert_equals(box.space.test:get{1}, {1})
    end)
end

--
-- The queue can be released by parts, from its front. The rest stays held.
--
g.test_release_flush_first = function(cg)
    local link = cg.link
    local queue = link.down.queue
    link.down:hold()
    cg.node1:exec(function()
        box.space.test:insert{1}
        box.space.test:insert{2}
    end)
    local insert_idx
    link.down:wait_held(function()
        insert_idx = find_inserts(queue)
        return #insert_idx == 2
    end)
    -- Everything up to the first row, the row included.
    link.down:release_flush_first(insert_idx[1])
    cg.node2:exec(function(timeout)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.space.test:get{1}, {1})
        end)
        t.assert_equals(box.space.test:get{2}, nil)
    end, {TIMEOUT})
    t.assert(replica_link.has_type(queue, box.iproto.type.INSERT))
    link.down:release_flush()
    cg.node2:wait_for_vclock_of(cg.node1)
    cg.node2:exec(function()
        t.assert_equals(box.space.test:get{2}, {2})
    end)
end

--
-- The upward direction carries the downstream's acks. While they are held, the
-- downstream has the row, and the upstream doesn't know that.
--
g.test_hold_up = function(cg)
    local link = cg.link
    link.up:hold()
    local lsn = cg.node1:exec(function()
        box.space.test:insert{1}
        return box.info.lsn
    end)
    cg.node2:wait_for_vclock_of(cg.node1)
    link.up:wait_held(function(queue)
        return replica_link.has_type(queue, box.iproto.type.OK)
    end)
    cg.node1:exec(function(node2_id, lsn)
        local downstream = box.info.replication[node2_id].downstream
        t.assert_lt(downstream.vclock[box.info.id] or 0, lsn)
    end, {cg.node2:get_instance_id(), lsn})
    link.up:release_flush()
    cg.node1:wait_for_downstream_to(cg.node2)
end

--
-- A wait fails when its condition isn't met in time. The time is the timeout
-- given to the call, or the link's one by default.
--
g.test_wait_timeout = function(cg)
    local link = cg.link
    link.down:hold()
    t.assert_error(link.down.wait_held, link.down, function()
        return false
    end, 0.01)
    -- Which timeout a wait takes is seen at the retrying helper it is built
    -- on: the durations aren't measured.
    local retrying = t.helpers.retrying
    local timeouts = {}
    t.helpers.retrying = function(config, ...)
        table.insert(timeouts, config.timeout)
        return retrying(config, ...)
    end
    local ok, err = pcall(function()
        link.down:wait_held(0)
        link.down:wait_held(0, TIMEOUT + 1)
    end)
    t.helpers.retrying = retrying
    t.assert(ok, err)
    t.assert_equals(timeouts, {TIMEOUT, TIMEOUT + 1})
    link.down:release_flush()
end

--
-- The queue is a plain array of decoded packets, and what is in it at the
-- release is what the downstream gets. The rows are taken out, looked at, and
-- put back.
--
g.test_take_out = function(cg)
    local link = cg.link
    local queue = link.down.queue
    link.down:hold()
    cg.node1:exec(function()
        box.space.test:insert{1}
        box.space.test:insert{2}
    end)
    link.down:wait_held(function()
        return #find_inserts(queue) == 2
    end)
    local rows = replica_link.take_out(queue, is_insert)
    t.assert_equals(find_inserts(queue), {})
    t.assert_equals(#rows, 2)
    t.assert_equals(rows[1].body[box.iproto.key.TUPLE], {1})
    t.assert_equals(rows[2].body[box.iproto.key.TUPLE], {2})
    table.insert(queue, rows[1])
    table.insert(queue, rows[2])
    link.down:release_flush()
    cg.node2:wait_for_vclock_of(cg.node1)
    cg.node2:exec(function()
        t.assert_equals(box.space.test:select(), {{1}, {2}})
    end)
end

--
-- The held packets of a connection which got closed are dropped: there is
-- nobody to get them. A new connection through the same link starts clean.
--
g.test_closed_connection = function(cg)
    local link = cg.link
    local queue = link.down.queue
    link.down:hold()
    cg.node1:exec(function()
        box.space.test:insert{1}
    end)
    link.down:wait_held(function()
        return #find_inserts(queue) == 1
    end)
    cg.node2:update_box_cfg{replication = {}}
    link.down:wait_held(function()
        return #queue == 0
    end)
    link.down:release_flush()
    -- The row wasn't delivered, the upstream sends it again.
    cg.node2:exec(function()
        t.assert_equals(box.space.test:get{1}, nil)
    end)
    cg.node2:update_box_cfg{replication = {link.uri}}
    cg.node2:wait_for_vclock_of(cg.node1)
    cg.node2:exec(function()
        t.assert_equals(box.space.test:get{1}, {1})
    end)
end

--
-- A new replica joins through a link: the snapshot stream and the
-- registration pass it as well as the subscription does.
--
g.test_join = function(cg)
    cg.node1:exec(function()
        box.space.test:insert{1}
    end)
    local link = replica_link.new{
        upstream = cg.node1.net_box_uri,
        alias = 'link_3_from_1',
        replica_set_id = cg.replica_set.id,
        timeout = TIMEOUT,
    }
    link:start()
    -- The same timeout as on node 1: its relay sends the heartbeats that
    -- rarely. The bootstrap leader is named: by default a join wants a
    -- connection to each registered node, node 2 here.
    local node3 = cg.replica_set:build_and_add_server{
        alias = 'node3',
        box_cfg = {
            replication = {link.uri},
            replication_timeout = 1000,
            bootstrap_strategy = 'config',
            bootstrap_leader = link.uri,
        },
    }
    node3:start()
    local node1_id = cg.node1:get_instance_id()
    local node3_id = node3:get_instance_id()
    t.helpers.retrying({timeout = TIMEOUT}, function()
        node3:assert_follows_upstream(node1_id)
    end)
    node3:exec(function()
        t.assert_equals(box.space.test:get{1}, {1})
    end)
    cg.node1:exec(function()
        box.space.test:insert{2}
    end)
    node3:wait_for_vclock_of(cg.node1)
    node3:exec(function()
        t.assert_equals(box.space.test:get{2}, {2})
    end)

    node3:drop()
    cg.replica_set:delete_server('node3')
    link:stop()
    cg.node1:exec(function(id)
        box.space._cluster:delete{id}
    end, {node3_id})
end
