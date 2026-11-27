local t = require('luatest')
local server = require('luatest.server')
local g = t.group('replication_mode')

--
-- Store both space policies under old compatibility before enabling the
-- requested replication mode.
--
local function start_server(mode)
    local instance = server:new({box_cfg = {memtx_use_mvcc_engine = true}})
    instance:start()
    instance:exec(function(mode)
        local compat = require('compat')
        t.assert(compat.box_cfg_replication_mode:is_old())
        box.schema.space.create('s', {is_sync = true}):create_index('pk')
        box.schema.space.create('a'):create_index('pk')
        box.schema.space.create('l', {is_local = true}):create_index('pk')
        box.schema.space.create('tmp', {temporary = true}):create_index('pk')
        box.cfg{
            replication_mode = mode,
            election_mode = mode == 'sync' and 'manual' or 'off',
            replication_synchro_timeout = 120,
        }
        compat.box_cfg_replication_mode = 'new'
        if mode == 'sync' then
            box.ctl.promote()
            box.ctl.wait_rw()
        end
    end, {mode})
    return instance
end

g.before_all(function(cg)
    cg.server = start_server('sync')
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_api_and_stored_flags = function(cg)
    cg.server:exec(function()
        local compat = require('compat')
        for _, value in ipairs({true, false}) do
            local opts = {is_sync = value}
            t.assert_error_msg_contains("unexpected option 'is_sync'",
                                        box.begin, opts)
            t.assert_error_msg_contains("unexpected option 'is_sync'",
                                        box.commit, opts)
            t.assert_error_msg_contains("unexpected option 'is_sync'",
                                        box.atomic, opts, function() end)
            t.assert_error_msg_contains("unexpected option 'is_sync'",
                                        box.schema.space.create, 'bad', opts)
            t.assert_error_msg_contains("unexpected option 'is_sync'",
                                        box.space.s.alter, box.space.s, opts)
        end
        local s, state = box.space.s, box.space.s.state
        t.assert_equals(s.is_sync, nil)
        t.assert_equals(state.is_sync, nil)
        t.assert_equals(box.space._space:get{s.id}.flags.is_sync, true)
        local features = box.iproto.protocol_features
        t.assert_equals(features.is_sync, nil)
        compat.box_tuple_extension = 'old'
        t.assert_equals(features.is_sync, nil)
        t.assert_equals(features.call_ret_tuple_extension, nil)
        t.assert_equals(features.call_arg_tuple_extension, nil)
        compat.box_consider_system_spaces_synchronous = 'new'
        t.assert_equals(box.space._schema.state.is_sync, nil)
        compat.box_cfg_replication_mode = 'old'
        t.assert_equals(s.is_sync, true)
        t.assert_equals(state.is_sync, true)
        t.assert_equals(box.space.a.is_sync, false)
        t.assert_equals(features.is_sync, true)
        t.assert_equals(features.call_ret_tuple_extension, nil)
        t.assert_equals(features.call_arg_tuple_extension, nil)
        compat.box_tuple_extension = 'default'
        t.assert_equals(features.is_sync, true)
        t.assert_equals(features.call_ret_tuple_extension, true)
        t.assert_equals(features.call_arg_tuple_extension, true)
        compat.box_cfg_replication_mode = 'new'
        t.assert_equals(features.is_sync, nil)
        t.assert(rawequal(features, box.iproto.protocol_features))
        t.assert_equals(s.is_sync, nil)
        compat.box_consider_system_spaces_synchronous = 'default'
    end)
end

g.test_c_api_aborts = function(cg)
    cg.server:exec(function()
        local ffi = require('ffi')
        ffi.cdef('void box_txn_make_sync(void);')
        box.begin()
        box.space.a:insert{1}
        ffi.C.box_txn_make_sync()
        t.assert_error_covers({code = box.error.TXN_ROLLBACK}, box.commit)
        t.assert_equals(box.space.a:get{1}, nil)
        ffi.C.box_txn_make_sync()
    end)
end

g.test_election_validation_and_transition = function(cg)
    cg.server:exec(function()
        local election = box.info.election
        t.assert_error_covers({code = box.error.CFG}, box.cfg,
                              {election_mode = 'off'})
        t.assert_equals(box.cfg.replication_mode, 'sync')
        t.assert_equals(box.cfg.election_mode, 'manual')
        t.assert_equals(box.info.election, election)
        t.assert_error_covers({code = box.error.CFG}, box.cfg,
                              {replication_mode = 'async'})
        t.assert_equals(box.cfg.replication_mode, 'sync')
        t.assert_equals(box.cfg.election_mode, 'manual')
        t.assert_error_covers({code = box.error.CFG}, box.cfg,
                              {replication_mode = 'mixed'})
        t.assert_equals(box.cfg.replication_mode, 'sync')
        t.assert_error_covers({code = box.error.CFG}, box.cfg, {
            replication_mode = 'mixed',
            election_mode = 'voter',
        })
        t.assert_equals(box.cfg.replication_mode, 'sync')
        t.assert_equals(box.cfg.election_mode, 'manual')
        t.assert_equals(box.info.election, election)
        box.space.a:insert{1}
        box.ctl.txn_sync()
        box.cfg{
            replication_mode = 'async',
            election_mode = 'off',
        }
        t.assert(box.info.ro)
        t.assert_error_covers({reason = 'synchro'}, box.space.a.insert,
                              box.space.a, {2})
        box.space.l:insert{2}
        t.assert_error_covers({code = box.error.UNSUPPORTED}, box.ctl.promote)
        box.ctl.demote()
        box.ctl.wait_rw()
        box.space.a:insert{2}
        t.assert_equals(box.info.synchro.queue.owner, 0)
        t.assert_error_covers({code = box.error.UNSUPPORTED}, box.ctl.promote)
        t.assert_error_covers({code = box.error.CFG}, box.cfg,
                              {replication_mode = 'sync'})
        t.assert_equals(box.cfg.replication_mode, 'async')
        t.assert_equals(box.cfg.election_mode, 'off')
        election = box.info.election
        for _, mode in ipairs({'manual', 'voter', 'candidate'}) do
            t.assert_error_covers({code = box.error.CFG}, box.cfg,
                                  {election_mode = mode})
            t.assert_equals(box.cfg.election_mode, 'off')
            t.assert_equals(box.info.election, election)
        end
        box.cfg{
            replication_mode = 'sync',
            election_mode = 'manual',
        }
        box.ctl.promote()
        box.ctl.wait_rw()
        box.space.a:delete{1}
        box.space.a:delete{2}
        box.space.l:delete{2}
    end)
end

--
-- A rejected async switch must preserve the election configuration and
-- leadership so the pending synchronous transaction can still be confirmed.
--
g.test_pending_queue_blocks_async = function(cg)
    cg.server:exec(function()
        box.cfg{replication_synchro_quorum = 2}
        box.begin()
        box.space.a:insert{1}
        box.commit{wait = 'submit'}
        box.ctl.wal_sync()
        t.assert_error_msg_contains('empty synchronous queue', box.cfg, {
            replication_mode = 'async',
            election_mode = 'off',
        })
        t.assert_equals(box.cfg.replication_mode, 'sync')
        t.assert_equals(box.cfg.election_mode, 'manual')
        t.assert_equals(box.info.election.state, 'leader')
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
        box.cfg{replication_synchro_quorum = 1}
        box.ctl.txn_sync()
        box.space.a:delete{1}
    end)
end

g.test_iproto_key_presence = function(cg)
    local conn = require('net.box').connect(cg.server.net_box_uri)
    local stream = conn:new_stream()
    for _, request in ipairs({'BEGIN', 'COMMIT'}) do
        for _, value in ipairs({false, true}) do
            local key = box.iproto.key
            local packet = box.iproto.encode_packet({
                [key.REQUEST_TYPE] = box.iproto.type[request],
                [key.STREAM_ID] = stream._stream_id,
                [key.SYNC] = stream:_next_sync(),
            }, {[key.IS_SYNC] = value})
            t.assert_error_msg_contains("unexpected option 'is_sync'",
                                        stream._inject, stream, packet)
        end
    end
    stream:begin()
    stream:rollback()
    conn:close()
end

g.test_failed_compat_switch = function(cg)
    cg.server:exec(function()
        local compat = require('compat')
        compat.box_cfg_replication_mode = 'old'
        box.cfg{
            replication_mode = 'sync',
            election_mode = 'off',
        }
        t.assert_error_covers({code = box.error.CFG}, function()
            compat.box_cfg_replication_mode = 'new'
        end)
        t.assert(compat.box_cfg_replication_mode:is_old())
        t.assert_equals(box.space.s.is_sync, true)
        t.assert_equals(box.iproto.protocol_features.is_sync, true)
        box.cfg{
            replication_mode = 'async',
            election_mode = 'manual',
        }
        t.assert_error_covers({code = box.error.CFG}, function()
            compat.box_cfg_replication_mode = 'new'
        end)
        t.assert(compat.box_cfg_replication_mode:is_old())
        box.cfg{
            replication_mode = 'sync',
            election_mode = 'manual',
        }
        compat.box_cfg_replication_mode = 'new'
        box.ctl.promote()
        box.ctl.wait_rw()
    end)
end

--
-- Reject the whole group while promotion is pending. In particular, the
-- requested election change must not interrupt that promotion.
--
g.test_promotion_blocks_switch = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function()
        local fiber = require('fiber')
        box.ctl.demote()
        box.error.injection.set('ERRINJ_TXN_LIMBO_BEGIN_DELAY_COUNTDOWN', 0)
        local f = fiber.create(function()
            fiber.self():set_joinable(true)
            box.ctl.promote()
        end)
        t.helpers.retrying({}, function()
            t.assert(box.error.injection.get('ERRINJ_TXN_LIMBO_BEGIN_DELAY'))
        end)
        t.assert_error_msg_contains('unfinished promotion or demotion',
            box.cfg, {
                replication_mode = 'async',
                election_mode = 'off',
            })
        t.assert_equals(box.cfg.replication_mode, 'sync')
        t.assert_equals(box.cfg.election_mode, 'manual')
        box.error.injection.set('ERRINJ_TXN_LIMBO_BEGIN_DELAY', false)
        t.assert(f:join())
        t.helpers.retrying({}, function()
            t.assert_not(box.info.synchro.queue.busy)
        end)
        t.assert_equals(box.info.election.state, 'leader')
        t.assert_equals(box.info.synchro.queue.owner, box.info.id)
    end)
end

local modes = t.group('replication_mode_transactions',
                      {{mode = 'sync'}, {mode = 'async'}})

modes.before_all(function(cg)
    cg.server = start_server(cg.params.mode)
end)

modes.after_all(function(cg) cg.server:drop() end)

modes.test_transactions = function(cg)
    cg.server:exec(function(mode)
        box.space.l:insert{1}
        box.space.tmp:insert{1}
        box.cfg{replication_synchro_quorum = 2}
        for _, name in ipairs({'s', 'a'}) do
            box.begin()
            box.space[name]:insert{1}
            box.commit{wait = 'submit'}
        end
        box.ctl.wal_sync()
        t.assert_equals(box.info.synchro.queue.len, mode == 'sync' and 2 or 0)
        box.cfg{replication_synchro_quorum = 1}
        box.ctl.txn_sync()
        for _, name in ipairs({'s', 'a', 'l', 'tmp'}) do
            t.assert_equals(box.space[name]:get{1}, {1})
            box.space[name]:delete{1}
        end
    end, {cg.params.mode})
end

modes.test_linearizable = function(cg)
    cg.server:exec(function(mode)
        for _, name in ipairs({'s', 'a', 'l', 'tmp'}) do
            box.begin{txn_isolation = 'linearizable'}
            if mode == 'async' and (name == 's' or name == 'a') then
                t.assert_error_covers({code = box.error.UNSUPPORTED},
                                      box.space[name].get, box.space[name], {1})
            else
                t.assert_equals(box.space[name]:get{1}, nil)
            end
            box.rollback()
        end
    end, {cg.params.mode})
end

local startup = t.group('replication_mode_startup',
                       {{mode = 'sync'}, {mode = 'async'}})

startup.before_all(function(cg)
    cg.server = server:new({box_cfg = {
        replication_mode = cg.params.mode,
        election_mode = cg.params.mode == 'sync' and 'manual' or 'off',
    }, env = {
        TARANTOOL_RUN_BEFORE_BOX_CFG =
            "require('compat').box_cfg_replication_mode = 'new'",
    }})
    cg.server:start()
end)

startup.after_all(function(cg) cg.server:drop() end)

startup.test_bootstrap_and_restart = function(cg)
    cg.server:exec(function()
        box.schema.space.create('s'):create_index('pk')
        box.space.s:insert{1}
        box.ctl.txn_sync()
        box.snapshot()
    end)
    cg.server:restart()
    cg.server:exec(function()
        t.assert_equals(box.space.s:get{1}, {1})
        t.assert_equals(box.space.s.is_sync, nil)
        t.assert_equals(box.space.s.state.is_sync, nil)
    end)
end

local features_group = t.group('replication_mode_features')

features_group.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {app_threads = 1},
        net_box_credentials = {user = 'admin'},
        env = {TARANTOOL_RUN_BEFORE_BOX_CFG =
            "require('compat').box_cfg_replication_mode = 'new'"},
    })
    cg.server:start()
end)

features_group.after_all(function(cg)
    cg.server:drop()
end)

--
-- Switching replication compatibility must also refresh feature tables that
-- application threads cached before the switch.
--
features_group.test_application_threads = function(cg)
    cg.server:exec(function()
        t.assert_equals(box.iproto.protocol_features.is_sync, nil)
        rawset(_G, 'saved_protocol_features', box.iproto.protocol_features)
    end, {}, {_thread_id = 1})
    for _, value in ipairs({'old', 'new'}) do
        local expected = cg.server:exec(function(new_compat)
            require('compat').box_cfg_replication_mode = new_compat
            local features = box.iproto.protocol_features
            t.assert_equals(features.is_sync,
                            new_compat == 'old' and true or nil)
            return features
        end, {value})
        cg.server:exec(function(expected_features)
            t.helpers.retrying({}, function()
                t.assert_equals(box.iproto.protocol_features, expected_features)
            end)
            t.assert(rawequal(_G.saved_protocol_features,
                             box.iproto.protocol_features))
        end, {expected}, {_thread_id = 1})
    end
    cg.server:exec(function()
        _G.saved_protocol_features = nil
    end, {}, {_thread_id = 1})
end
