local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')
local g1 = t.group('group1')

local wait_timeout = 60

g1.before_all = function(lg)
    lg.master = server:new({alias = 'master'})
    lg.master:start()
end

g1.after_all = function(lg)
    lg.master:drop()
end

--
-- When an instance failed to apply cfg{replication_anon = false}, it used to
-- report itself as non-anon in the ballot anyway. Shouldn't be so.
--
g1.test_ballot_on_deanon_fail = function(lg)
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

--
-- gh-9916: txns being applied from the master during the name change process
-- could crash the replica or cause "double LSN" error in release.
--
g1.test_txns_replication_during_registration = function(lg)
    t.tarantool.skip_if_not_debug()
    lg.master:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
    end)
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
        box.error.injection.set("ERRINJ_WAL_DELAY_COUNTDOWN", 0)
    end)
    lg.master:exec(function()
        box.space.test:replace{1}
    end)
    replica:exec(function(timeout)
        -- One txn from master is being applied by the replica. Not yet
        -- reflected in its vclock.
        t.helpers.retrying({timeout = timeout}, function()
            if box.error.injection.get("ERRINJ_WAL_DELAY") then
                return
            end
            error("No txn from master")
        end)
        local fiber = require('fiber')
        -- Send registration request while having a remote txn being committed
        -- from the same master.
        local f = fiber.create(function() box.cfg{replication_anon = false} end)
        f:set_joinable(true)
        rawset(_G, 'test_f', f)
    end, {wait_timeout})
    lg.master:exec(function(timeout)
        t.helpers.retrying({timeout = timeout}, function()
            if box.space._cluster:count() == 2 then
                return
            end
            error("No registration request from replica")
        end)
        -- Bump the LSN again. To make it easier later to wait when the replica
        -- gets all previous data from the master.
        box.space.test:replace{2}
    end, {wait_timeout})
    local replica_id = replica:exec(function()
        box.error.injection.set("ERRINJ_WAL_DELAY", false)
        local ok, err = _G.test_f:join()
        _G.test_f = nil
        t.assert_equals(err, nil)
        t.assert(ok)
        local id = box.info.id
        t.assert_not_equals(id, 0)
        return id
    end)
    replica:wait_for_vclock_of(lg.master)
    replica:exec(function()
        t.assert(box.space.test:get{2}, {2})
    end)

    -- Cleanup.
    replica:drop()
    lg.master:exec(function(id)
        box.space.test:drop()
        box.space._cluster:delete{id}
    end, {replica_id})
end

--
-- gh-10561:
-- Anonymous replicas don't participate in elections.
--

local function build_cluster(lg, election_mode, replication_anon)
    lg.cluster = cluster:new({})
    lg.master = lg.cluster:build_and_add_server({alias = 'master'})
    lg.replica_cfg = {
        replication = server.build_listen_uri('master', lg.cluster.id),
        election_mode = election_mode,
        replication_anon = replication_anon,
        read_only = true,
    }
    lg.replica = lg.cluster:build_and_add_server({
        alias = 'replica',
        box_cfg = lg.replica_cfg,
    })
    lg.cluster:start()
    lg.master:exec(function() box.ctl.promote() end)
    t.helpers.retrying({timeout = wait_timeout}, function()
        lg.replica:assert_follows_upstream(lg.master:get_instance_id())
    end)
end

local g2 = t.group('group2')

g2.before_all(function(lg)
    build_cluster(lg, 'off', true)
end)

g2.after_all(function(lg)
    lg.cluster:drop()
end)

for _, mode in ipairs({'candidate', 'manual', 'voter'}) do
    g2["test_anon_replica_startup_with_election_mode_" .. mode] = function(lg)
        -- box_check_config is called only during the first box.cfg,
        -- so we need a restart here to cover it
        local ok, _ = pcall(function()
            lg.replica:restart({box_cfg = {
                replication = server.build_listen_uri('master', lg.cluster.id),
                election_mode = mode,
                replication_anon = true,
                read_only = true,
            }}, {wait_until_ready = true})
        end)
        t.assert(not ok)
        lg.replica:restart({box_cfg = lg.replica_cfg})
    end
end

for _, mode in ipairs({'candidate', 'manual', 'voter'}) do
    g2["test_anon_replica_switch_to_election_mode_" .. mode] =
        function(lg)
            lg.replica:exec(function(election_mode)
                local err_msg = "Incorrect value for option " ..
                    "'election_mode': the value may only be set to 'off' " ..
                    "when 'replication_anon' is set to true"
                t.assert_error_msg_equals(err_msg, box.cfg,
                    {election_mode = election_mode})
            end, {mode})
        end
end

g2.test_anon_replica_promote_unsupported = function(lg)
    lg.replica:exec(function()
        local err_msg = "replication_anon=true " ..
            "does not support manual elections"
        t.assert_error_msg_equals(err_msg, box.ctl.promote)
        t.assert_error_msg_equals(err_msg, box.ctl.demote)
    end)
end

local g3 = t.group('group3')

for _, mode in ipairs({'candidate', 'manual', 'voter'}) do
    g3["test_replica_with_" .. mode .. "election_mode_switch_to_anon"] =
        function(lg)
            -- We are forced to rebuild the cluster every time, because if the
            -- replica was not previously registered in the cluster, it cannot
            -- connect as a non-anonymous replica
            build_cluster(lg, mode, false)
            lg.replica:exec(function()
                local err_msg = "Incorrect value for option " ..
                    "'replication_anon': the value may be set to true only " ..
                    "when 'election_mode' is set to 'off'"
                t.assert_error_msg_equals(err_msg, box.cfg,
                    {replication_anon = true})
            end)
            lg.cluster:drop()
        end
end
