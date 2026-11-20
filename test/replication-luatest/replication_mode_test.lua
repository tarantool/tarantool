local t = require('luatest')
local server = require('luatest.server')
local new_compat = "require('compat').box_cfg_replication_mode = 'new'"

local migration = t.group('replication_mode', {{anon = false}, {anon = true}})

migration.before_all(function(cg)
    cg.master = server:new({alias = 'master', box_cfg = {
        replication_mode = 'sync',
        election_mode = 'manual',
        replication_timeout = 0.1,
    }, env = {TARANTOOL_RUN_BEFORE_BOX_CFG = new_compat}})
    cg.master:start()
    cg.master:exec(function()
        box.schema.space.create('s'):create_index('pk')
        box.space.s:insert{1}
    end)
    cg.replica = server:new({alias = 'replica', box_cfg = {
        replication = cg.master.net_box_uri,
        replication_mode = 'sync',
        election_mode = cg.params.anon and 'off' or 'voter',
        replication_anon = cg.params.anon,
        read_only = true,
        replication_timeout = 0.1,
    }, env = {TARANTOOL_RUN_BEFORE_BOX_CFG = new_compat}})
    cg.replica:start()
    if not cg.params.anon then
        cg.master:exec(function(uri)
            box.cfg{replication = uri}
            box.ctl.wait_rw()
        end, {cg.replica.net_box_uri})
    end
end)

migration.after_all(function(cg)
    cg.replica:drop()
    cg.master:drop()
end)

migration.test_migration_with_retained_owner = function(cg)
    cg.master:exec(function()
        box.cfg{read_only = true}
        box.ctl.txn_sync()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
        box.cfg{
            replication_mode = 'async',
            election_mode = 'off',
        }
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
    end)
    cg.replica:exec(function()
        box.ctl.txn_sync()
        box.cfg{
            replication_mode = 'async',
            election_mode = 'off',
        }
        t.assert_not_equals(box.info.synchro.queue.owner, 0)
    end)
    cg.master:exec(function()
        box.ctl.demote()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
        box.cfg{read_only = false}
        box.space.s:insert{2}
        box.ctl.txn_sync()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
        box.cfg{read_only = true}
    end)
    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        t.assert_equals(box.space.s:select(), {{1}, {2}})
    end)
    cg.master:exec(function()
        box.cfg{
            replication_mode = 'sync',
            election_mode = 'manual',
        }
    end)
    cg.replica:exec(function(anon)
        box.cfg{
            replication_mode = 'sync',
            election_mode = anon and 'off' or 'voter',
        }
    end, {cg.params.anon})
    cg.master:exec(function()
        box.ctl.promote()
        box.cfg{read_only = false}
        box.space.s:insert{3}
        box.ctl.txn_sync()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
    end)
    cg.replica:exec(function()
        t.assert_equals(box.space.s:select(), {{1}, {2}, {3}})
    end)
    cg.master:exec(function()
        box.space.s:delete{2}
        box.space.s:delete{3}
        box.ctl.txn_sync()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
    end)
end

migration.test_legacy_to_new = function(cg)
    cg.replica:exec(function()
        require('compat').box_cfg_replication_mode = 'old'
    end)
    cg.master:exec(function()
        require('compat').box_cfg_replication_mode = 'old'
        box.space.s:alter{is_sync = true}
        box.space.s:insert{2}
        box.ctl.txn_sync()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
        require('compat').box_cfg_replication_mode = 'new'
        box.space.s:insert{3}
        box.ctl.txn_sync()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
    end)
    cg.replica:exec(function()
        require('compat').box_cfg_replication_mode = 'new'
        t.assert_equals(box.space.s:select(), {{1}, {2}, {3}})
        t.assert_equals(box.space.s.is_sync, nil)
        local flags = box.space._space:get{box.space.s.id}.flags
        t.assert_equals(flags.is_sync, true)
    end)
    cg.replica:exec(function()
        require('compat').box_cfg_replication_mode = 'old'
    end)
    cg.master:exec(function()
        local compat = require('compat')
        compat.box_cfg_replication_mode = 'old'
        box.space.s:alter{is_sync = false}
        box.space.s:delete{2}
        box.space.s:delete{3}
        box.ctl.txn_sync()
        box.ctl.replication_sync{include_anon = true, timeout = 120}
        compat.box_cfg_replication_mode = 'new'
    end)
    cg.replica:exec(function()
        require('compat').box_cfg_replication_mode = 'new'
    end)
end

local recovery = t.group('replication_mode_recovery')

recovery.test_async_rejects_pending_recovery = function(cg)
    cg.server = server:new({box_cfg = {
        replication_mode = 'sync',
        election_mode = 'manual',
        replication_synchro_timeout = 120,
    }, env = {TARANTOOL_RUN_BEFORE_BOX_CFG = new_compat}})
    cg.server:start()
    cg.server:exec(function()
        box.schema.space.create('s'):create_index('pk')
        box.snapshot()
        box.cfg{replication_synchro_quorum = 2}
        box.begin()
        box.space.s:insert{1}
        box.commit{wait = 'submit'}
        box.ctl.wal_sync()
        t.assert_equals(box.info.synchro.queue.len, 1)
    end)
    cg.server.process:kill('KILL')
    cg.server:restart({box_cfg = {
        replication_mode = 'async', election_mode = 'off',
    }}, {wait_until_ready = false})
    t.helpers.retrying({}, function()
        t.assert_not(cg.server.process:is_alive())
        t.assert(cg.server:grep_log(
            'async requires an empty synchronous queue after recovery',
            nil, {plain = true}))
    end)
    cg.server:drop()
end

local anon = t.group('replication_mode_anon')

anon.before_all(function(cg)
    cg.master = server:new({alias = 'master'})
    cg.master:start()
end)

anon.after_all(function(cg)
    cg.master:drop()
end)

--
-- Registration is irreversible, so each registration test needs its own
-- anonymous replica. The master stays in legacy mode to accept both policies.
--
local function new_replica(cg, mode)
    local replica = server:new({
        alias = 'replica',
        box_cfg = {
            replication = cg.master.net_box_uri,
            replication_anon = true,
            replication_mode = mode,
            read_only = true,
            replication_timeout = 0.1,
            replication_sync_timeout = 120,
        },
        env = {
            TARANTOOL_RUN_BEFORE_BOX_CFG =
                "require('compat').box_cfg_replication_mode = 'new'",
        },
    })
    replica:start()
    return replica
end

--
-- Remove completed registrations so the shared master has the same state for
-- the next test.
--
local function drop_replica(cg, replica)
    local id = replica:exec(function() return box.info.id end)
    replica:drop()
    if id ~= 0 then
        cg.master:exec(function(id) box.space._cluster:delete{id} end, {id})
    end
end

--
-- Validate the requested group before registration, which cannot be undone.
-- Registering a synchronous replica also requires enabling elections.
--
anon.test_invalid_group_does_not_register = function(cg)
    local replica = new_replica(cg, 'sync')
    replica:exec(function()
        t.assert_error_covers({code = box.error.CFG}, box.cfg, {
            replication_anon = false,
            replication_mode = 'async',
            election_mode = 'voter',
        })
        t.assert_equals(box.info.id, 0)
        t.assert(box.cfg.replication_anon)
        t.assert_equals(box.cfg.replication_mode, 'sync')
        t.assert_equals(box.cfg.election_mode, 'off')
        t.assert_error_covers({code = box.error.CFG}, box.cfg,
                              {replication_anon = false})
        t.assert_equals(box.info.id, 0)
        t.assert(box.cfg.replication_anon)
    end)
    -- Registration reconnects using the requested mode, matching the master.
    cg.master:exec(function()
        require('compat').box_cfg_replication_mode = 'new'
    end)
    replica:exec(function()
        box.cfg{
            replication_anon = false,
            replication_mode = 'async',
            election_mode = 'off',
        }
        t.assert_not_equals(box.info.id, 0)
        t.assert_not(box.cfg.replication_anon)
        t.assert_equals(box.cfg.replication_mode, 'async')
        t.assert_equals(box.cfg.election_mode, 'off')
    end)
    cg.master:exec(function()
        require('compat').box_cfg_replication_mode = 'old'
    end)
    drop_replica(cg, replica)
end

--
-- Restore the mode on registration failure and keep elections disabled until
-- registration succeeds, including in the advertised ballot.
--
anon.test_registration_failure_and_retry = function(cg)
    local replica = new_replica(cg, 'sync')
    cg.master:exec(function()
        rawset(_G, 'reject_registration', function()
            error('Reject registration')
        end)
        box.space._cluster:on_replace(_G.reject_registration)
    end)
    replica:exec(function()
        for _, mode in ipairs({'async', 'sync'}) do
            local iproto = box.iproto
            t.assert_error_msg_contains('Reject registration', box.cfg, {
                replication_anon = false,
                replication_mode = mode,
                election_mode = mode == 'sync' and 'manual' or 'off',
            })
            t.assert_equals(box.info.id, 0)
            t.assert(box.cfg.replication_anon)
            t.assert_equals(box.cfg.replication_mode, 'sync')
            t.assert_equals(box.cfg.election_mode, 'off')
            local ballot = box.watch_once('internal.ballot')[iproto.key.BALLOT]
            t.assert(ballot[iproto.ballot_key.IS_ANON])
            t.assert_not(ballot[iproto.ballot_key.CAN_LEAD])
            t.assert_equals(ballot[iproto.ballot_key.REPLICATION_MODE], 2)
        end
    end)
    cg.master:exec(function()
        box.space._cluster:on_replace(nil, _G.reject_registration)
        _G.reject_registration = nil
    end)
    replica:exec(function()
        box.cfg{
            replication_anon = false,
            election_mode = 'manual',
        }
        t.assert_not_equals(box.info.id, 0)
        t.assert_not(box.cfg.replication_anon)
        t.assert_equals(box.cfg.replication_mode, 'sync')
        t.assert_equals(box.cfg.election_mode, 'manual')
        local ballot = box.watch_once('internal.ballot')[box.iproto.key.BALLOT]
        t.assert_not(ballot[box.iproto.ballot_key.IS_ANON])
        t.assert(ballot[box.iproto.ballot_key.CAN_LEAD])
    end)
    replica:wait_for_vclock_of(cg.master)
    drop_replica(cg, replica)
end

anon.test_async_registration = function(cg)
    local replica = new_replica(cg, 'async')
    replica:exec(function()
        box.cfg{replication_anon = false}
        t.assert_not_equals(box.info.id, 0)
        t.assert_not(box.cfg.replication_anon)
        t.assert_equals(box.cfg.replication_mode, 'async')
        t.assert_equals(box.cfg.election_mode, 'off')
    end)
    drop_replica(cg, replica)
end

local promotion = t.group('replication_mode_promote')

promotion.before_all(function(cg)
    cg.server = server:new({box_cfg = {
        election_mode = 'off',
        election_timeout = 120,
        replication_mode = 'async',
        replication_synchro_timeout = 120,
    }})
    cg.server:start()
end)

promotion.after_all(function(cg)
    cg.server:drop()
end)

--
-- The Raft worker runs election triggers before claiming the limbo. While a
-- trigger yields, the public caller can finish its Raft wait and start waiting
-- for the limbo. The promotion guard must cover this gap as well as elections.
--
promotion.test_wait_for_limbo_blocks_compat = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local compat = require('compat')
        local entered, release = fiber.channel(1), fiber.channel(1)
        local function on_election()
            if box.info.election.state == 'leader' then
                entered:put(true)
                release:get()
            end
        end
        box.ctl.on_election(on_election)
        local promote = fiber.new(box.ctl.promote)
        promote:set_joinable(true)
        t.assert(entered:get(120))

        -- Resume the caller once with Raft already in the leader state. Its
        -- next yield must be in the limbo wait, since the worker is still held.
        local csw = promote:csw()
        promote:wakeup()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_gt(promote:csw(), csw)
        end)
        t.assert_equals(box.info.synchro.queue.owner, 0)
        t.assert_not(box.info.synchro.queue.busy)
        t.assert_error_covers({code = box.error.CFG}, function()
            compat.box_cfg_replication_mode = 'new'
        end)
        t.assert(compat.box_cfg_replication_mode:is_old())

        box.ctl.on_election(nil, on_election)
        release:put(true)
        t.assert(promote:join(120))
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        compat.box_cfg_replication_mode = 'new'
        t.assert(box.info.ro)
        box.ctl.demote()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        t.assert_not(box.info.ro)
        compat.box_cfg_replication_mode = 'old'
    end)
end

promotion.test_cancel_during_election_releases_guard = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        local compat = require('compat')
        local term = box.info.election.term
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
        local promote = fiber.new(box.ctl.promote)
        promote:set_joinable(true)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_gt(box.info.election.term, term)
        end)
        t.assert_equals(box.info.election.state, 'follower')
        t.assert_error_covers({code = box.error.CFG}, function()
            compat.box_cfg_replication_mode = 'new'
        end)
        t.assert(compat.box_cfg_replication_mode:is_old())

        promote:cancel()
        local ok, err = promote:join(120)
        t.assert_not(ok)
        t.assert_equals(err.type, 'FiberIsCancelled')
        compat.box_cfg_replication_mode = 'new'
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        box.ctl.wal_sync()
        t.assert_equals(box.info.synchro.queue.owner, 0)
        compat.box_cfg_replication_mode = 'old'
        box.ctl.promote()
        box.ctl.demote()
        t.assert_not(box.info.ro)
    end)
end

local ballot = t.group('replication_mode_ballot')

ballot.before_all(function(cg)
    cg.master = server:new({alias = 'master', box_cfg = {
        replication_mode = 'async', replication_timeout = 0.1,
    }, env = {TARANTOOL_RUN_BEFORE_BOX_CFG = new_compat}})
    cg.master:start()
end)

ballot.after_all(function(cg)
    cg.master:drop()
end)

--
-- Recovery and reconnect cases register a replica before rejecting its mode.
-- Remove that registration so the next bootstrap starts with the same peers.
--
local function drop_ballot_replica(cg)
    cg.replica:drop()
    cg.replica = nil
    cg.master:exec(function()
        for _, tuple in box.space._cluster:pairs() do
            if tuple[1] ~= box.info.id then
                box.space._cluster:delete{tuple[1]}
            end
        end
    end)
end

local function assert_startup_mismatch(cg)
    t.helpers.retrying({}, function()
        t.assert_not(cg.replica.process:is_alive())
        t.assert(cg.replica:grep_log('Replication mode mismatch'))
        t.assert_not(cg.replica:grep_log('recover from'))
        t.assert_not(cg.replica:grep_log('building secondary indexes'))
    end)
end

for _, strategy in ipairs({'auto', 'legacy'}) do
    ballot['test_bootstrap_mismatch_' .. strategy] = function(cg)
        cg.replica = server:new({alias = 'replica', box_cfg = {
            replication = cg.master.net_box_uri,
            replication_mode = 'sync', election_mode = 'voter',
            bootstrap_strategy = strategy,
            replication_connect_quorum = strategy == 'legacy' and 0 or nil,
            replication_connect_timeout = 0.5,
        }, env = {TARANTOOL_RUN_BEFORE_BOX_CFG = new_compat}})
        cg.replica:start({wait_until_ready = false})
        assert_startup_mismatch(cg)
        drop_ballot_replica(cg)
    end
end

ballot.test_recovery_mismatch = function(cg)
    cg.replica = server:new({alias = 'replica', box_cfg = {
        replication = cg.master.net_box_uri,
        read_only = true,
    }, env = {TARANTOOL_RUN_BEFORE_BOX_CFG = new_compat}})
    cg.replica:start()
    cg.replica:restart({box_cfg = {
        replication = cg.master.net_box_uri,
        replication_mode = 'sync', election_mode = 'voter',
        replication_connect_timeout = 0.5,
    }}, {wait_until_ready = false})
    assert_startup_mismatch(cg)
    drop_ballot_replica(cg)
end

--
-- Legacy recovery with a zero connection quorum need not receive any ballots.
-- The applier detects the mismatch after recovery.
--
ballot.test_recovery_with_zero_quorum = function(cg)
    cg.replica = server:new({alias = 'replica', box_cfg = {
        replication = cg.master.net_box_uri,
        read_only = true,
    }, env = {TARANTOOL_RUN_BEFORE_BOX_CFG = new_compat}})
    cg.replica:start()
    cg.replica:restart({box_cfg = {
        replication = cg.master.net_box_uri,
        replication_mode = 'sync',
        election_mode = 'voter',
        bootstrap_strategy = 'legacy',
        replication_connect_quorum = 0,
    }})
    cg.replica:exec(function()
        t.assert_equals(box.info.status, 'running')
    end)
    t.helpers.retrying({}, function()
        t.assert(cg.replica:grep_log('Replication mode mismatch'))
    end)
    drop_ballot_replica(cg)
end

ballot.test_runtime_retry_and_legacy_peer = function(cg)
    cg.replica = server:new({alias = 'replica', box_cfg = {
        replication = cg.master.net_box_uri,
        read_only = true, replication_timeout = 0.1,
    }, env = {TARANTOOL_RUN_BEFORE_BOX_CFG = new_compat}})
    cg.replica:start()
    cg.replica:exec(function()
        box.cfg{
            replication_mode = 'sync',
            election_mode = 'voter',
        }
        t.assert_equals(box.info.replication[1].upstream.status, 'follow')
    end)
    cg.master:restart()
    cg.replica:exec(function()
        t.helpers.retrying({}, function()
            local upstream = box.info.replication[1].upstream
            t.assert_equals(upstream.status, 'loading')
            t.assert_str_contains(upstream.message, 'Replication mode mismatch')
        end)
    end)
    -- Omission means legacy, even after a dedicated mode was advertised.
    cg.master:exec(function()
        require('compat').box_cfg_replication_mode = 'old'
    end)
    cg.replica:exec(function()
        t.helpers.retrying({}, function()
            t.assert_equals(box.info.replication[1].upstream.status, 'follow')
        end)
    end)
    drop_ballot_replica(cg)
    cg.master:exec(function()
        require('compat').box_cfg_replication_mode = 'new'
    end)
end

--
-- Hot standby recovers local data before connecting to peers. Keep an upstream
-- alive after takeover to check that the mode mismatch is still rejected then.
--
ballot.test_hot_standby_checks_ballot_after_takeover = function(cg)
    cg.replica = server:new({alias = 'replica', box_cfg = {
        replication = cg.master.net_box_uri,
        read_only = true,
    }, env = {TARANTOOL_RUN_BEFORE_BOX_CFG = new_compat}})
    cg.replica:start()
    cg.replica:exec(function() box.snapshot() end)
    local standby = server:new({alias = 'standby', box_cfg = {
        memtx_dir = cg.replica.workdir,
        wal_dir = cg.replica.workdir,
        vinyl_dir = cg.replica.workdir,
        hot_standby = true,
        replication = cg.master.net_box_uri,
        replication_mode = 'sync',
        election_mode = 'voter',
        replication_connect_timeout = 0.5,
    }, env = {TARANTOOL_RUN_BEFORE_BOX_CFG = new_compat}})
    standby:start({wait_until_ready = false})
    t.helpers.retrying({}, function()
        t.assert(standby:grep_log('Entering hot standby mode'))
    end)
    t.assert(standby.process:is_alive())
    t.assert_not(standby:grep_log('Replication mode mismatch'))
    cg.replica:stop()
    t.helpers.retrying({}, function()
        t.assert_not(standby.process:is_alive())
        t.assert(standby:grep_log('Replication mode mismatch'))
    end)
    standby:drop()
    drop_ballot_replica(cg)
end

local wal = t.group('replication_mode_wal', {{mode = 'sync'}, {mode = 'async'}})

--
-- Rejected records remain in WAL and stop appliers permanently. Each case
-- needs its own replicaset so these histories cannot affect the next one.
--
local function start_wal_cluster(cg)
    cg.master = server:new({alias = 'master', box_cfg = {
        replication_mode = cg.params.mode,
        election_mode = cg.params.mode == 'sync' and 'manual' or 'off',
        replication_synchro_quorum = 1,
        replication_timeout = 0.1,
    }, env = {TARANTOOL_RUN_BEFORE_BOX_CFG = new_compat}})
    cg.master:start()
    cg.master:exec(function()
        box.schema.space.create('s'):create_index('pk')
    end)
    cg.replica = server:new({alias = 'replica', box_cfg = {
        replication = cg.master.net_box_uri,
        replication_mode = cg.params.mode,
        election_mode = cg.params.mode == 'sync' and 'voter' or 'off',
        read_only = true,
        replication_timeout = 0.1,
    }, env = {TARANTOOL_RUN_BEFORE_BOX_CFG = new_compat}})
    cg.replica:start()
    cg.replica:wait_for_vclock_of(cg.master)
end

local function drop_wal_cluster(cg)
    if cg.sink ~= nil then
        cg.sink:drop()
        cg.sink = nil
    end
    cg.replica:drop()
    cg.master:drop()
end

local function assert_stopped(replica, upstream_id, vclock, reason)
    replica:exec(function(id, clock)
        t.helpers.retrying({}, function()
            local upstream = box.info.replication[id].upstream
            t.assert_equals(upstream.status, 'stopped')
            t.assert_str_contains(upstream.message, 'Replication mode WAL')
        end)
        t.assert_equals(box.info.vclock[1], clock[1])
        t.assert_equals(box.space.s:select(), {})
    end, {upstream_id, vclock})
    t.assert(replica:grep_log(reason, nil, {plain = true}))
end

wal.test_incompatible_transaction = function(cg)
    start_wal_cluster(cg)
    local mode = cg.params.mode == 'sync' and 'async' or 'sync'
    local clock = cg.replica:exec(function(target)
        box.cfg{
            replication_mode = target,
            election_mode = target == 'sync' and 'voter' or 'off',
        }
        return box.info.vclock
    end, {mode})
    cg.master:exec(function() box.space.s:insert{1} end)
    assert_stopped(cg.replica, 1, clock, 'transaction in ' .. mode .. ' mode')
    -- A local mode correction does not restart a permanently stopped applier.
    cg.replica:exec(function(target)
        box.cfg{
            replication_mode = target,
            election_mode = target == 'sync' and 'voter' or 'off',
        }
        t.assert_equals(box.info.replication[1].upstream.status, 'stopped')
        local replication = box.cfg.replication
        box.cfg{replication = {}}
        box.cfg{replication = replication}
    end, {cg.params.mode})
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function() t.assert_equals(box.space.s:select(), {{1}}) end)
    drop_wal_cluster(cg)
end

wal.test_legacy_originator = function(cg)
    start_wal_cluster(cg)
    cg.master:exec(function()
        require('compat').box_cfg_replication_mode = 'old'
    end)
    cg.replica:exec(function()
        local replication = box.cfg.replication
        box.cfg{replication = {}}
        box.cfg{replication = replication}
    end)
    -- The legacy writer can originate an asynchronous transaction in either
    -- mode. With sync mode, its own queue ownership makes this term valid.
    cg.master:exec(function() box.space.s:insert{1} end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function() t.assert_equals(box.space.s:select(), {{1}}) end)
    drop_wal_cluster(cg)
end

wal.test_promotion = function(cg)
    t.skip_if(cg.params.mode ~= 'async')
    start_wal_cluster(cg)
    local clock = cg.replica:get_vclock()
    cg.master:exec(function()
        box.cfg{
            replication_mode = 'sync',
            election_mode = 'manual',
        }
        box.ctl.promote()
    end)
    assert_stopped(cg.replica, 1, clock, 'promotion in async mode')
    cg.replica:exec(function()
        t.assert_equals(box.info.synchro.queue.owner, 0)
    end)
    drop_wal_cluster(cg)
end

wal.test_forwarded_unknown_originator = function(cg)
    t.skip_if(cg.params.mode ~= 'async')
    start_wal_cluster(cg)
    -- R1 knows M, R2 only knows R1. R2 must not use R1's ballot for M's rows.
    cg.sink = server:new({alias = 'sink', box_cfg = {
        replication = cg.replica.net_box_uri,
        replication_mode = 'async',
        replication_anon = true,
        read_only = true,
    }, env = {TARANTOOL_RUN_BEFORE_BOX_CFG = new_compat}})
    cg.sink:start()
    cg.replica:exec(function()
        box.cfg{
            replication_mode = 'sync',
            election_mode = 'voter',
        }
    end)
    cg.master:exec(function()
        box.cfg{
            replication_mode = 'sync',
            election_mode = 'manual',
        }
        box.ctl.promote()
        box.space.s:insert{1}
    end)
    cg.sink:wait_for_vclock_of(cg.master)
    cg.sink:exec(function() t.assert_equals(box.space.s:select(), {{1}}) end)
    drop_wal_cluster(cg)
end

for _, nop in ipairs({false, true}) do
    wal['test_forwarded_known_originator_nop_' .. tostring(nop)] = function(cg)
        t.skip_if(cg.params.mode ~= 'async')
        t.tarantool.skip_if_not_debug()
        start_wal_cluster(cg)
        cg.replica:exec(function(make_nop)
            if make_nop then
                box.cfg{
                    read_only = false,
                    replication_skip_conflict = true,
                }
                box.space.s:insert{1}
            end
        end, {nop})
        cg.sink = server:new({alias = 'sink', box_cfg = {
            replication = {cg.master.net_box_uri, cg.replica.net_box_uri},
            bootstrap_strategy = 'config',
            bootstrap_leader = cg.replica.net_box_uri,
            replication_anon = true,
            read_only = true,
        }, env = {TARANTOOL_RUN_BEFORE_BOX_CFG = new_compat}})
        cg.sink:start()
        cg.replica:exec(function()
            box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', true)
        end)
        local clock = cg.sink:exec(function()
            box.cfg{replication_mode = 'sync'}
            return box.info.vclock
        end)
        cg.master:exec(function() box.space.s:insert{1} end)
        cg.sink:exec(function()
            t.helpers.retrying({}, function()
                t.assert_equals(box.info.replication[1].upstream.status,
                                'stopped')
            end)
        end)
        cg.replica:exec(function()
            box.error.injection.set('ERRINJ_RELAY_SEND_DELAY', false)
        end)
        if nop then
            cg.sink:wait_for_vclock_of(cg.master)
            cg.sink:exec(function()
                local upstream = box.info.replication[2].upstream
                t.assert_equals(upstream.status, 'follow')
                t.assert_equals(box.space.s:select(), {{1}})
            end)
        else
            assert_stopped(cg.sink, 2, clock, 'transaction in sync mode')
        end
        drop_wal_cluster(cg)
    end
end

wal.test_final_join = function(cg)
    t.skip_if(cg.params.mode ~= 'async')
    t.tarantool.skip_if_not_debug()
    start_wal_cluster(cg)
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_REPLICA_JOIN_DELAY', true)
    end)
    local uuid = require('uuid').str()
    cg.sink = server:new({alias = 'sink', box_cfg = {
        instance_uuid = uuid,
        replication = cg.master.net_box_uri,
        bootstrap_strategy = 'legacy',
        read_only = true,
    }, env = {TARANTOOL_RUN_BEFORE_BOX_CFG = new_compat}})
    cg.sink:start({wait_until_ready = false})
    t.helpers.retrying({}, function()
        t.assert(cg.master:grep_log('replica ' .. uuid:gsub('%-', '%%-')))
    end)
    cg.master:exec(function()
        box.cfg{
            replication_mode = 'sync',
            election_mode = 'manual',
        }
        box.ctl.promote()
        box.error.injection.set('ERRINJ_REPLICA_JOIN_DELAY', false)
    end)
    t.helpers.retrying({}, function()
        t.assert_not(cg.sink.process:is_alive())
        t.assert(cg.sink:grep_log('Replication mode WAL mismatch'))
    end)
    drop_wal_cluster(cg)
end
