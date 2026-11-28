local t = require('luatest')
local replica_client = require('test.replication-luatest.lib.replica_client')
local replica_server = require('test.replication-luatest.lib.replica_server')
local qsync = require('test.replication-luatest.lib.qsync_tools')
local tools = require('test.replication-luatest.lib.tools')

local g = t.group()

-- Every wait in the file. Nothing here is expected to take long; a wait
-- running out is a failure, not slowness.
local TIMEOUT = 60

--
-- A PROMOTE waiting for acks is stored as it came, and the limbo relies on its
-- shape from then on. A row a correct node never makes - without its row LSN,
-- or claiming its author confirmed its own rows above the row itself - must be
-- refused on arrival, with the applier stopped on a clear error, not asserted
-- on later.
--
-- The rows come from node 2, which is not an instance: a replica registered at
-- node 1 by hand, and a server feeding node 1's applier under its identity.
-- The rows are built by the test from scratch.
--

local key = box.iproto.key
local rows = replica_server.rows
local setmap = require('internal.utils').setmap

-- A member of the replica set with no instance behind it: joined to node 1 -
-- a snapshot fetched, then registered - so its id is in _cluster. An origin
-- for the rows the test builds.
local function join_replica(cg)
    local node1_uri = cg.nodes[1].net_box_uri
    local snapshot = replica_client.fetch_snapshot(node1_uri, {
        timeout = TIMEOUT,
    })
    return replica_client.register(node1_uri, {
        uuid = snapshot.uuid,
        vclock = snapshot.vclock,
        timeout = TIMEOUT,
    })
end

-- The debug-only box.info.synchro introspection.
g.before_all(function()
    t.tarantool.skip_if_not_debug()
end)

g.before_each(function(cg)
    qsync.build_replica_set(cg, {
        replication_timeout = 0.1,
        replication_synchro_quorum = 1,
        replication_synchro_timeout = TIMEOUT,
        election_mode = 'manual',
    }, {node_count = 1, glob_timeout = TIMEOUT})
    local node1 = cg.nodes[1]
    node1:exec(function()
        box.ctl.promote()
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.schema.space.create('s', {is_sync = true}):create_index('p')
        box.space.s:replace{1}
    end)
    local node2 = join_replica(cg)
    cg.node2_id = node2.instance_id
    cg.node2_server = replica_server.new{
        alias = 'node2_server',
        replica_set_id = cg.replica_set.id,
        uuid = node2.uuid,
        instance_id = node2.instance_id,
        timeout = TIMEOUT,
    }
    -- Node 1 follows the server. It sends no heartbeats, the timeout has to
    -- cover the silence.
    cg.node2_server:start()
    node1:update_box_cfg{
        replication_timeout = TIMEOUT,
        replication = {cg.node2_server.uri},
    }
    cg.session = cg.node2_server:wait_subscribe()
    node1:exec(function(node2_id)
        t.helpers.retrying({timeout = _G.TIMEOUT}, function()
            t.assert_equals(box.info.replication[node2_id].upstream.status,
                            'follow')
        end)
    end, {cg.node2_id})
end)

g.after_each(function(cg)
    cg.node2_server:stop()
    tools.drop_replica_set(cg.replica_set, TIMEOUT)
end)

-- A PROMOTE waiting for acks from node 2 as node 2 would make it: its first
-- row, the next term, replacing node 1 at what node 1 has confirmed. The
-- packet, to be sent as it is or bent first.
local function build_promote(cg)
    local node1 = cg.nodes[1]
    local node1_id = node1:get_instance_id()
    local node2_id = cg.node2_id
    local term, node1_term, confirm_lsn = node1:exec(function()
        return box.info.election.term + 1,
               box.info.synchro.term_map[box.info.id],
               box.info.synchro.confirmed_vclock[box.info.id]
    end)
    return rows.promote{
        origin = node2_id,
        lsn = 1,
        term = term,
        owner = node1_id,
        confirm_lsn = confirm_lsn,
        confirmed_vclock = setmap{[node1_id] = confirm_lsn},
        term_map = setmap{[node1_id] = node1_term, [node2_id] = term},
        wait_ack = true,
    }
end

local function wait_promote_pending(node, origin_id, term)
    node:exec(function(origin_id, term)
        t.helpers.retrying({timeout = _G.TIMEOUT}, function()
            local slot = box.info.synchro.nodes[origin_id]
            t.assert_not_equals(slot, nil)
            t.assert_not_equals(slot.promote, nil)
            t.assert_equals(slot.promote.term, term)
        end)
    end, {origin_id, term})
end

local function wait_upstream_stopped(node, upstream_id, message)
    node:exec(function(upstream_id, message)
        t.helpers.retrying({timeout = _G.TIMEOUT}, function()
            local upstream = box.info.replication[upstream_id].upstream
            t.assert_equals(upstream.status, 'stopped')
        end)
        local upstream = box.info.replication[upstream_id].upstream
        t.assert_str_contains(upstream.message, message)
    end, {upstream_id, message})
end

-- Nothing of the row is in the limbo, and the node is fine.
local function assert_nothing_saved(cg)
    cg.nodes[1]:exec(function(node2_id)
        local slot = box.info.synchro.nodes[node2_id]
        t.assert(slot == nil or slot.promote == nil)
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.space.s:select(), {{1}})
    end, {cg.node2_id})
end

--
-- The row as a correct node makes it is saved pending: the server works, and
-- the refusals below are about the bends.
--
g.test_well_formed_promote_is_saved = function(cg)
    local promote = build_promote(cg)
    cg.session:send(promote)
    wait_promote_pending(cg.nodes[1], cg.node2_id, promote.body[key.TERM])
    cg.nodes[1]:exec(function(node2_id)
        t.assert_not(box.info.synchro.nodes[node2_id].promote.is_poisoned)
        t.assert_equals(box.info.replication[node2_id].upstream.status,
                        'follow')
    end, {cg.node2_id})
end

--
-- The row LSN key of a checkpoint row, set to zero: the row has no LSN to be
-- tracked by.
--
g.test_promote_without_row_lsn = function(cg)
    local promote = build_promote(cg)
    promote.body[key.ROW_LSN] = 0
    cg.session:send(promote)
    wait_upstream_stopped(cg.nodes[1], cg.node2_id, 'Invalid MsgPack')
    assert_nothing_saved(cg)
end

--
-- The author's own component of the confirmed vclock above the row.
--
g.test_promote_confirming_above_its_row = function(cg)
    local promote = build_promote(cg)
    promote.body[key.VCLOCK][cg.node2_id] = promote.header[key.LSN] + 1
    cg.session:send(promote)
    wait_upstream_stopped(cg.nodes[1], cg.node2_id,
                          'confirmed vclock above its own row')
    assert_nothing_saved(cg)
end

--
-- A PROMOTE whose term map gives a term to one node while another node's
-- pending PROMOTE holds it. Replication can't deliver such a row: the holder's
-- PROMOTE sits earlier in the author's journal, so it arrives first and is
-- refused as a reuse of the term. The row is a peer's invention and must be
-- refused when its CONFIRM tries to apply the map, not asserted on afterwards.
--
-- Node 3 has a PROMOTE in term 7 pending on node 1. Node 4's PROMOTE in term 8
-- replaces node 2, with the map saying node 2 held term 7.
--
g.test_promote_term_map_claiming_a_pending_term = function(cg)
    local node1 = cg.nodes[1]
    local node1_id = node1:get_instance_id()
    local node2_id = cg.node2_id
    local node3_id = join_replica(cg).instance_id
    local node4_id = join_replica(cg).instance_id
    local node1_term, confirm_lsn = node1:exec(function()
        return box.info.synchro.term_map[box.info.id],
               box.info.synchro.confirmed_vclock[box.info.id]
    end)
    cg.session:send(rows.promote{
        origin = node3_id,
        lsn = 1,
        term = 7,
        owner = node1_id,
        confirm_lsn = confirm_lsn,
        confirmed_vclock = setmap{[node1_id] = confirm_lsn},
        term_map = setmap{[node1_id] = node1_term, [node3_id] = 7},
        wait_ack = true,
    })
    wait_promote_pending(node1, node3_id, 7)
    local node4_promote_lsn = 1
    cg.session:send(rows.promote{
        origin = node4_id,
        lsn = node4_promote_lsn,
        term = 8,
        owner = node2_id,
        confirm_lsn = 0,
        confirmed_vclock = setmap{[node1_id] = confirm_lsn},
        term_map = setmap{
            [node1_id] = node1_term, [node2_id] = 7, [node4_id] = 8,
        },
        wait_ack = true,
    })
    wait_promote_pending(node1, node4_id, 8)
    cg.session:send(rows.confirm{
        origin = node4_id,
        lsn = node4_promote_lsn + 1,
        owner = node4_id,
        confirm_lsn = node4_promote_lsn,
    })
    wait_upstream_stopped(node1, node2_id, string.format(
        'term map gives the term 7 to instance %d, held by instance %d',
        node2_id, node3_id))
    node1:exec(function(node2_id, node3_id, node4_id)
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        t.assert_equals(box.info.synchro.nodes[node3_id].promote.term, 7)
        t.assert_equals(box.info.synchro.nodes[node4_id].promote.term, 8)
        t.assert_equals(box.info.synchro.term_map[node2_id], nil)
    end, {node2_id, node3_id, node4_id})
end
