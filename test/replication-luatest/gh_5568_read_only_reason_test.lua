local t = require('luatest')
local cluster = require('test.luatest_helpers.cluster')
local helpers = require('test.luatest_helpers')

--
-- gh-5568: ER_READONLY needs to carry additional info explaining the reason
-- why the error happened. It should help the clients to do better error logging
-- and to use the provided info to find the actual leader if the reason is in
-- being not a leader.
--

--
-- Make functions which will create and destroy a cluster. Usage of the closures
-- is a workaround for luatest hooks not accepting their group as a parameter.
--
local function make_create_cluster(g) return function()
    g.cluster = cluster:new({})
    local master_uri = helpers.instance_uri('master')
    local replica_uri = helpers.instance_uri('replica')
    local replication = {master_uri, replica_uri}
    local box_cfg = {
        listen = master_uri,
        replication = replication,
        -- To speed up new term when try to elect a new leader.
        replication_timeout = 0.1,
    }
    g.master = g.cluster:build_server({alias = 'master', box_cfg = box_cfg})

    box_cfg.listen = replica_uri
    g.replica = g.cluster:build_server({alias = 'replica', box_cfg = box_cfg})

    g.cluster:add_server(g.master)
    g.cluster:add_server(g.replica)
    g.cluster:start()
end end

local function make_destroy_cluster(g) return function()
    g.cluster:drop()
end end

--
-- This group's test cases leave instances in a valid state after which they can
-- be reused.
--
local g = t.group('gh-5568-read-only-reason1')

g.before_all(make_create_cluster(g))
g.after_all(make_destroy_cluster(g))

local read_only_msg = "Can't modify data on a read-only instance - "

--
-- Read-only because of box.cfg{read_only = true}.
--
g.test_read_only_reason_cfg = function(g)
    local ok, err = g.master:exec(function()
        box.cfg{read_only = true}
        local ok, err = pcall(box.schema.create_space, 'test')
        return ok, err:unpack()
    end)
    t.assert(not ok, 'fail ddl')
    t.assert_str_contains(err.message, read_only_msg..
                          'box.cfg.read_only is true')
    t.assert_covers(err, {
        reason = 'state',
        state = 'read_only',
        code = box.error.READONLY,
        type = 'ClientError'
    }, 'reason is read_only')

    -- Cleanup.
    g.master:exec(function()
        box.cfg{read_only = false}
    end)
end

--
-- Read-only because is an orphan.
--
g.test_read_only_reason_orphan = function(g)
    local fake_uri = helpers.instance_uri('fake')
    local old_timeout, ok, err = g.master:exec(function(fake_uri)
        -- Make connect-quorum impossible to satisfy using a fake instance.
        local old_timeout = box.cfg.replication_connect_timeout
        local repl = table.copy(box.cfg.replication)
        table.insert(repl, fake_uri)
        box.cfg{
            replication = repl,
            replication_connect_timeout = 0.001,
        }
        local ok, err = pcall(box.schema.create_space, 'test')
        return old_timeout, ok, err:unpack()
    end, {fake_uri})
    t.assert(not ok, 'fail ddl')
    t.assert_str_contains(err.message, read_only_msg..'it is an orphan')
    t.assert_covers(err, {
        reason = 'state',
        state = 'orphan',
        code = box.error.READONLY,
        type = 'ClientError'
    }, 'reason is orphan')

    -- Cleanup.
    g.master:exec(function(old_timeout)
        local repl = table.copy(box.cfg.replication)
        table.remove(repl)
        box.cfg{
            replication = repl,
            replication_connect_timeout = old_timeout,
        }
    end, {old_timeout})
end

--
-- Read-only because is not a leader. Does not know the leader.
--
g.test_read_only_reason_election_no_leader = function(g)
    local ok, err = g.master:exec(function()
        box.cfg{election_mode = 'voter'}
        local ok, err = pcall(box.schema.create_space, 'test')
        return ok, err:unpack()
    end)
    t.assert(not ok, 'fail ddl')
    t.assert(err.term, 'has term')
    t.assert_str_contains(err.message, read_only_msg..('state is election '..
                          '%s with term %s'):format(err.state, err.term))
    t.assert_covers(err, {
        reason = 'election',
        state = 'follower',
        code = box.error.READONLY,
        type = 'ClientError'
    }, 'reason is election, no leader')
    t.assert(not err.leader_id, 'no leader id')
    t.assert(not err.leader_uuid, 'no leader uuid')

    -- Cleanup.
    g.master:exec(function()
        box.cfg{election_mode = 'off'}
    end)
end

--
-- Read-only because is not a leader. Knows the leader.
--
g.test_read_only_reason_election_has_leader = function(g)
    g.master:exec(function()
        box.cfg{election_mode = 'candidate'}
    end)
    g.master:wait_election_leader()
    g.replica:wait_election_leader_found()
    local ok, err = g.replica:exec(function()
        box.cfg{election_mode = 'voter'}
        local ok, err = pcall(box.schema.create_space, 'test')
        return ok, err:unpack()
    end)
    t.assert(not ok, 'fail ddl')
    t.assert(err.term, 'has term')
    t.assert_str_contains(err.message, read_only_msg..('state is election '..
                          '%s with term %s, leader is %s (%s)'):format(
                          err.state, err.term, err.leader_id, err.leader_uuid))
    t.assert_covers(err, {
        reason = 'election',
        state = 'follower',
        leader_id = g.master:instance_id(),
        leader_uuid = g.master:instance_uuid(),
        code = box.error.READONLY,
        type = 'ClientError'
    }, 'reason is election, has leader')

    -- Cleanup.
    g.master:exec(function()
        box.cfg{election_mode = 'off'}
        box.ctl.demote()
    end)
    g.replica:exec(function()
        box.cfg{election_mode = 'off'}
    end)
end

--
-- Read-only because does not own the limbo. Knows the owner.
--
g.test_read_only_reason_synchro = function(g)
    g.master:exec(function()
        box.ctl.promote()
    end)

    t.helpers.retrying({}, function()
        assert(g.replica:exec(function()
            return box.info.synchro.queue.owner ~= 0
        end))
    end)

    local ok, err = g.replica:exec(function()
        local ok, err = pcall(box.schema.create_space, 'test2')
        return ok, err:unpack()
    end)
    t.assert(not ok, 'fail ddl')
    t.assert(err.term, 'has term')
    t.assert_str_contains(err.message, read_only_msg..('synchro queue with '..
                          'term %s belongs to %s (%s)'):format(err.term,
                          err.queue_owner_id, err.queue_owner_uuid))
    t.assert_covers(err, {
        reason = 'synchro',
        queue_owner_id = g.master:instance_id(),
        queue_owner_uuid = g.master:instance_uuid(),
        code = box.error.READONLY,
        type = 'ClientError'
    }, 'reason is synchro, has owner')

    -- Cleanup.
    g.master:exec(function()
        box.ctl.demote()
    end)
end

--
-- This group's test cases leave instances in an invalid state after which they
-- should be re-created.
--
g = t.group('gh-5568-read-only-reason2')

g.before_each(make_create_cluster(g))
g.after_each(make_destroy_cluster(g))

--
-- Read-only because is not a leader. Knows the leader, but not its UUID.
--
g.test_read_only_reason_election_has_leader_no_uuid = function(g)
    g.replica:exec(function()
        box.cfg{election_mode = 'voter'}
    end)
    g.master:exec(function()
        box.cfg{election_mode = 'candidate'}
    end)
    g.master:wait_election_leader()
    g.replica:wait_election_leader_found()
    local leader_id = g.master:instance_id()

    g.master:exec(function()
        box.space._cluster:run_triggers(false)
        box.space._cluster:delete{box.info.id}
    end)

    t.helpers.retrying({}, function()
        assert(g.replica:exec(function(leader_id)
            return box.space._cluster:get{leader_id} == nil
        end, {leader_id}))
    end)

    local ok, err = g.replica:exec(function()
        local ok, err = pcall(box.schema.create_space, 'test')
        return ok, err:unpack()
    end)
    t.assert(not ok, 'fail ddl')
    t.assert(err.term, 'has term')
    t.assert(not err.leader_uuid, 'has no leader uuid')
    t.assert_str_contains(err.message, read_only_msg..('state is election %s '..
                          'with term %s, leader is %s'):format(err.state,
                          err.term, err.leader_id))
    t.assert_covers(err, {
        reason = 'election',
        state = 'follower',
        leader_id = leader_id,
        code = box.error.READONLY,
        type = 'ClientError'
    }, 'reason is election, has leader but no uuid')
end

--
-- Read-only because does not own the limbo. Knows the owner, but not its UUID.
--
g.test_read_only_reason_synchro_no_uuid = function(g)
    g.master:exec(function()
        box.ctl.promote()
        box.space._cluster:run_triggers(false)
        box.space._cluster:delete{box.info.id}
    end)

    local leader_id = g.master:instance_id()
    t.helpers.retrying({}, function()
        assert(g.replica:exec(function(leader_id)
            return box.info.synchro.queue.owner ~= 0 and
                   box.space._cluster:get{leader_id} == nil
        end, {leader_id}))
    end)

    local ok, err = g.replica:exec(function()
        local ok, err = pcall(box.schema.create_space, 'test')
        return ok, err:unpack()
    end)
    t.assert(not ok, 'fail ddl')
    t.assert(err.term, 'has term')
    t.assert(not err.queue_owner_uuid)
    t.assert_str_contains(err.message, read_only_msg..('synchro queue with '..
                          'term %s belongs to %s'):format(err.term,
                          err.queue_owner_id))
    t.assert_covers(err, {
        reason = 'synchro',
        queue_owner_id = leader_id,
        code = box.error.READONLY,
        type = 'ClientError'
    }, 'reason is synchro, has owner but no uuid')
end
