local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group('gh-11039-bootstrap-protocol-order')

--
-- gh-11039: the new bootstrap protocol for a named replica is
-- FETCH_SNAPSHOT -> initial checkpoint -> anonymous SUBSCRIBE (until the lag
-- is small) -> REGISTER -> named SUBSCRIBE.
--
-- The old single-connection JOIN used to register the replica in the middle
-- of the data stream: the master waited for ACKs of pending synchronous
-- transactions from a replica which could not send them until subscribed,
-- while the replica waited for CONFIRM to pass its initial checkpoint - a
-- deadlock of the master and the replica.
--

local test_timeout = 60

g.before_each(function(cg)
    cg.rs = replica_set:new{}
    cg.master = cg.rs:build_and_add_server{
        alias = 'master',
        box_cfg = {
            -- Formula 'N' means the quorum equals the number of
            -- registered replicas: while the bootstrapping replica is
            -- anonymous it is not counted and the master confirms its
            -- sync transactions itself; right after REGISTER the quorum
            -- grows and unconfirmed transactions would start waiting for
            -- ACKs from the replica - the very gh-11039 window.
            replication_synchro_quorum = 'N',
            replication_synchro_timeout = test_timeout,
            replication_timeout = 0.1,
            election_mode = 'manual',
        },
    }
    cg.master:start()
    cg.master:exec(function()
        box.ctl.promote()
        box.ctl.wait_rw()
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
    end)
end)

g.after_each(function(cg)
    cg.rs:drop()
end)

--
-- The named replica must bootstrap via REGISTER (i.e. through the new
-- protocol), not via JOIN. The master logs "registering replica" only in
-- box_process_register(), while the legacy path logs "joining replica".
--
g.test_new_protocol_is_used = function(cg)
    cg.replica = cg.rs:build_and_add_server{
        alias = 'replica',
        box_cfg = {
            replication = server.build_listen_uri('master', cg.rs.id),
            replication_timeout = 0.1,
        },
    }
    cg.replica:start()
    cg.replica:exec(function()
        t.assert_equals(box.info.status, 'running')
        t.assert_equals(box.info.anon, false)
        t.assert_not_equals(box.info.id, 0)
    end)
    t.assert(cg.master:grep_log('registering replica'))
    t.assert_not(cg.master:grep_log('joining replica'))
end

--
-- Bootstrap of a named replica must not deadlock while the master keeps
-- writing synchronous transactions. With the new protocol the replica
-- acknowledges the master's pending sync transactions while it is still
-- anonymous, so the initial checkpoint (created before any registration)
-- never waits for a CONFIRM, and the REGISTER stream only carries already
-- resolved data.
--
g.test_bootstrap_under_sync_load = function(cg)
    cg.master:exec(function()
        rawset(_G, 'writer_fiber', require('fiber').create(function()
            local i = 0
            while true do
                i = i + 1
                pcall(box.space.test.insert, box.space.test, {i})
                require('fiber').sleep(0.01)
            end
        end))
    end)

    cg.replica = cg.rs:build_and_add_server{
        alias = 'replica',
        box_cfg = {
            replication = server.build_listen_uri('master', cg.rs.id),
            replication_synchro_timeout = test_timeout,
            replication_timeout = 0.1,
        },
    }
    cg.replica:start()

    cg.replica:exec(function()
        t.assert_equals(box.info.status, 'running')
        t.assert_equals(box.info.synchro.queue.len, 0)
    end)
    t.helpers.retrying({timeout = test_timeout}, function()
        cg.replica:assert_follows_upstream(cg.master:get_instance_id())
    end)

    -- The replica must eventually carry all the rows written during its
    -- bootstrap.
    cg.master:exec(function()
        _G.writer_fiber:cancel()
        _G.writer_fiber = nil
    end)
    t.helpers.retrying({timeout = test_timeout}, function()
        local count = cg.master:exec(function()
            return box.space.test:count()
        end)[1]
        cg.replica:exec(function(expect)
            t.assert_equals(box.space.test:count(), expect)
        end, {count})
    end)
    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.len, 0)
    end)
end
