local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local function assert_ro_state(s, reason, details)
    s:exec(function(reason, details)
        t.assert_equals(box.info.ro, reason ~= nil)
        t.assert_equals(box.info.ro_reason, reason)
        t.assert_equals(box.info.ro_details, details)
        t.assert_equals(box.info().ro_details, details)
    end, {reason, details})
end

local function assert_cfg_error(s, cfg, message)
    local ok, err = s:exec(function(cfg)
        local ok, err = pcall(box.cfg, cfg)
        return ok, err:unpack()
    end, {cfg})
    t.assert(not ok)
    t.assert_str_contains(err.message, message)
end

local g_startup = t.group('startup')

g_startup.after_each(function(g)
    if g.server ~= nil then
        g.server:drop()
    end
end)

-- Initial configuration exposes custom details after recovering a snapshot.
g_startup.test_initial_details = function(g)
    g.server = server:new()
    g.server:start()
    g.server:exec(function() box.snapshot() end)
    g.server:stop()
    -- A standalone RO instance can recover, but cannot bootstrap itself.
    g.server.box_cfg = {
        read_only = true,
        ro_details = 'maintenance',
    }
    g.server:start()
    assert_ro_state(g.server, 'config', 'maintenance')
end

-- During recovery, the reason stays 'config' while errors retain synchro
-- diagnostics and omit custom details.
g_startup.test_recovery_synchro_diagnostics = function(g)
    g.server = server:new()
    g.server:start()
    g.server:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
        box.ctl.promote()
        box.snapshot()
    end)
    local run_before_cfg = [[
        require('trigger').set('box.ctl.on_recovery_state', 'ro_error',
            function(state)
                if state ~= 'wal_recovered' then
                    return
                end
                rawset(_G, 'recovery_ro_reason', box.info.ro_reason)
                rawset(_G, 'recovery_ro_details', box.info.ro_details)
                local ok, err = pcall(box.space.test.insert,
                                      box.space.test, {1})
                assert(not ok)
                rawset(_G, 'recovery_ro_error', err:unpack())
            end)
    ]]
    g.server:restart({env = {
        TARANTOOL_RUN_BEFORE_BOX_CFG = run_before_cfg,
    }})
    g.server:exec(function()
        t.assert_equals(rawget(_G, 'recovery_ro_reason'), 'config')
        t.assert_equals(rawget(_G, 'recovery_ro_details'), nil)
        local err = rawget(_G, 'recovery_ro_error')
        t.assert_covers(err, {
            code = box.error.READONLY,
            reason = 'config',
            state = 'follower',
            queue_owner_id = box.info.id,
        })
        t.assert_equals(err.details, nil)
        t.assert_type(err.term, 'number')
        t.assert_type(err.queue_term, 'number')
        t.assert_str_contains(err.message, 'state is election follower')
        t.assert_str_contains(err.message, 'synchro queue')
    end)
end

local g = t.group('single')

g.before_all(function()
    g.server = server:new()
    g.server:start()
end)

g.after_all(function()
    if g.server ~= nil then
        g.server:drop()
        g.server = nil
    end
end)

g.after_each(function()
    g.server:exec(function()
        box.cfg{read_only = false}
    end)
end)

-- Custom details are exposed without replacing the stable 'config' reason.
g.test_custom_details = function()
    g.server:exec(function()
        box.cfg{read_only = true, ro_details = 'maintenance'}
        t.assert_equals(box.cfg.ro_details, 'maintenance')
    end)
    assert_ro_state(g.server, 'config', 'maintenance')
end

-- Without custom text, introspection and read-only errors have no details.
g.test_default_details = function()
    g.server:exec(function()
        box.cfg{read_only = true}
        t.assert_equals(box.cfg.ro_details, nil)
        local ok, err = pcall(box.schema.create_space, 'test')
        t.assert_not(ok)
        t.assert_equals(err.reason, 'config')
        t.assert_equals(err.details, nil)
    end)
    assert_ro_state(g.server, 'config', nil)
end

-- Switching to RW clears details so they cannot leak into a later RO state.
g.test_stale_details_are_cleared = function()
    g.server:exec(function()
        box.cfg{read_only = true, ro_details = 'maintenance'}
        box.cfg{read_only = false}
        t.assert_equals(box.info.ro_details, nil)
        t.assert_equals(box.cfg.ro_details, nil)
        box.cfg{read_only = true}
    end)
    assert_ro_state(g.server, 'config', nil)
end

-- Details can change while staying RO; omitting them clears the previous text.
g.test_details_updated_and_cleared_while_ro = function()
    g.server:exec(function()
        box.cfg{read_only = true, ro_details = 'maintenance'}
        box.cfg{read_only = true, ro_details = 'backup'}
        t.assert_equals(box.info.ro_reason, 'config')
        t.assert_equals(box.info.ro_details, 'backup')
        box.cfg{read_only = true}
        t.assert_equals(box.cfg.ro_details, nil)
    end)
    assert_ro_state(g.server, 'config', nil)
end

-- Reconfiguring an unrelated option preserves the configured details.
g.test_unrelated_reload_preserves_details = function()
    g.server:exec(function()
        box.cfg{read_only = true, ro_details = 'maintenance'}
        box.cfg{too_long_threshold = box.cfg.too_long_threshold}
        t.assert_equals(box.cfg.ro_details, 'maintenance')
    end)
    assert_ro_state(g.server, 'config', 'maintenance')
end

-- Details require read_only = true in the same call, even when already RO.
g.test_details_require_read_only = function()
    assert_cfg_error(g.server, {ro_details = 'maintenance'},
                     'may be set only when read_only is true')
    g.server:exec(function()
        box.cfg{read_only = true, ro_details = 'maintenance'}
    end)
    assert_cfg_error(g.server, {ro_details = 'backup'},
                     'may be set only when read_only is true')
    assert_ro_state(g.server, 'config', 'maintenance')
end

-- Reject details supplied with read_only = false without changing the state.
g.test_details_reject_read_only_false = function()
    assert_cfg_error(g.server, {
        read_only = false,
        ro_details = 'maintenance',
    }, 'read_only is true')
    assert_ro_state(g.server, nil, nil)
end

-- Read-only errors include custom details but keep reason = 'config'.
g.test_custom_details_in_error = function()
    local err = g.server:exec(function()
        box.cfg{read_only = true, ro_details = 'maintenance'}
        local _, err = pcall(box.schema.create_space, 'test')
        return err:unpack()
    end)
    t.assert_covers(err, {
        reason = 'config',
        details = 'maintenance',
        code = box.error.READONLY,
        type = 'ClientError',
    })
    t.assert_str_contains(err.message,
                           'box.cfg.read_only is true - maintenance')
end

-- Reject text exceeding the 511-byte limit without changing the RO state.
g.test_details_too_long_are_rejected = function()
    assert_cfg_error(g.server, {
        read_only = true,
        ro_details = string.rep('x', 512),
    }, 'must not exceed')
    assert_ro_state(g.server, nil, nil)
end

-- Reject embedded zero bytes rather than silently truncating the details.
g.test_details_with_zero_byte_are_rejected = function()
    assert_cfg_error(g.server, {
        read_only = true,
        ro_details = 'maintenance\0backup',
    }, 'must not contain a zero byte')
    assert_ro_state(g.server, nil, nil)
end

-- Non-string details produce a configuration type error and leave state intact.
g.test_details_type_is_checked = function()
    assert_cfg_error(g.server, {
        read_only = true,
        ro_details = true,
    }, 'should be of type string')
    assert_ro_state(g.server, nil, nil)
end

-- The longest accepted text survives introspection and error-field round trips.
g.test_details_max_len_round_trip = function()
    local len = g.server:exec(function()
        local details = string.rep('x', 511)
        box.cfg{read_only = true, ro_details = details}
        local ok, err = pcall(box.schema.create_space, 'test')
        t.assert_not(ok)
        t.assert_equals(err.reason, 'config')
        t.assert_equals(err.details, details)
        return #box.info.ro_details
    end)
    t.assert_equals(len, 511)
end

-- Special characters are kept verbatim in box.cfg/box.info but escaped in the
-- ER_READONLY message, so it stays a single line.
g.test_special_chars_are_escaped_in_error = function()
    local raw = 'tab\there\nnew"quote"\\slash\r\b'
    local cfg_details, err = g.server:exec(function(raw)
        box.cfg{read_only = true, ro_details = raw}
        local _, err = pcall(box.schema.create_space, 'test')
        return box.cfg.ro_details, err:unpack()
    end, {raw})
    -- The original value is preserved as is.
    t.assert_equals(cfg_details, raw)
    t.assert_equals(err.reason, 'config')
    t.assert_equals(err.details, raw)
    assert_ro_state(g.server, 'config', raw)
    -- The message carries the escaped form and no raw control characters.
    t.assert_str_contains(err.message, 'box.cfg.read_only is true - ' ..
        'tab\\there\\nnew\\"quote\\"\\\\slash\\r\\b')
    for _, c in ipairs({'\n', '\t', '\r', '\b'}) do
        t.assert_not_str_contains(err.message, c)
    end
end

-- Priority cases: need a replicaset to drive 'synchro' via elections.
local g2 = t.group('priority')

g2.before_each(function()
    g2.cluster = cluster:new({})
    local master_uri = server.build_listen_uri('master', g2.cluster.id)
    local replica_uri = server.build_listen_uri('replica', g2.cluster.id)
    local box_cfg = {
        replication = {master_uri, replica_uri},
        replication_timeout = 0.1,
        bootstrap_strategy = 'legacy',
    }
    box_cfg.listen = master_uri
    g2.master = g2.cluster:build_and_add_server({alias = 'master',
                                                 box_cfg = box_cfg})
    box_cfg.listen = replica_uri
    g2.replica = g2.cluster:build_and_add_server({alias = 'replica',
                                                  box_cfg = box_cfg})
    g2.cluster:start()
    g2.cluster:wait_for_fullmesh()
end)

g2.after_each(function()
    g2.cluster:drop()
end)

-- Synchro restrictions hide stored configuration details; removing them makes
-- the details visible again without reconfiguring the text.
g2.test_synchro_hides_custom_details = function()
    g2.master:exec(function()
        box.cfg{election_mode = 'candidate', replication_synchro_quorum = 2}
    end)
    g2.replica:exec(function()
        box.cfg{read_only = true, ro_details = 'maintenance'}
        box.cfg{election_mode = 'voter'}
    end)
    g2.master:wait_for_election_leader()
    g2.replica:wait_until_election_leader_found()

    t.assert_equals(g2.replica:exec(function()
        return box.cfg.read_only
    end), true)
    assert_ro_state(g2.replica, 'synchro', nil)
    g2.replica:exec(function()
        t.assert_equals(box.cfg.ro_details, 'maintenance')
        local ok, err = pcall(box.schema.create_space, 'test')
        t.assert_not(ok)
        t.assert_equals(err.reason, 'synchro')
        t.assert_equals(err.details, nil)
        t.assert_not_str_contains(err.message, 'maintenance')
        box.cfg{election_mode = 'off'}
    end)
    g2.master:exec(function()
        box.cfg{election_mode = 'off'}
        box.ctl.demote()
    end)
    t.helpers.retrying({}, function()
        assert_ro_state(g2.replica, 'config', 'maintenance')
    end)
end

-- Orphan RO has no custom details; explicit configuration RO takes precedence
-- and exposes its own details.
g2.test_orphan_reason_when_not_ro = function()
    local fake_uri = server.build_listen_uri('fake', g2.cluster.id)
    g2.master:exec(function(fake_uri)
        local repl = table.copy(box.cfg.replication)
        table.insert(repl, fake_uri)
        box.cfg{replication = repl, replication_connect_timeout = 0.001}
    end, {fake_uri})

    t.helpers.retrying({timeout = 100}, function()
        t.assert_equals(g2.master:exec(function()
            return box.info.status
        end), 'orphan')
    end)
    t.assert_equals(g2.master:exec(function()
        return box.cfg.read_only
    end), false)
    assert_ro_state(g2.master, 'orphan', nil)
    g2.master:exec(function()
        local ok, err = pcall(box.schema.create_space, 'test')
        t.assert_not(ok)
        t.assert_equals(err.reason, 'orphan')
        t.assert_equals(err.details, nil)
        box.cfg{read_only = true, ro_details = 'maintenance'}
    end)
    assert_ro_state(g2.master, 'config', 'maintenance')
end
