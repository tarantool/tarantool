local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local function ro_reason(s)
    return s:exec(function() return box.info.ro_reason end)
end

local function assert_cfg_error(s, cfg, message)
    local ok, err = s:exec(function(cfg)
        local ok, err = pcall(box.cfg, cfg)
        return ok, err:unpack()
    end, {cfg})
    t.assert(not ok)
    t.assert_str_contains(err.message, message)
end

-- Single-instance cases: custom reason, default 'config', and error reporting.
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
        box.cfg{read_only = false, ro_reason = nil}
    end)
end)

-- Custom reason is passed and returned as is.
g.test_custom_reason = function()
    g.server:exec(function()
        box.cfg{read_only = true, ro_reason = 'custom reason'}
        t.assert_equals(box.cfg.ro_reason, 'custom reason')
    end)
    t.assert_equals(ro_reason(g.server), 'custom reason')
end

-- No reason is passed, so the default 'config' value is returned.
g.test_default_reason = function()
    g.server:exec(function()
        box.cfg{read_only = true}
        t.assert_equals(box.cfg.ro_reason, nil)
    end)
    t.assert_equals(ro_reason(g.server), 'config')
end

-- A stale custom reason must not leak: reconfiguring read_only without a fresh
-- ro_reason resets it to the default 'config'.
g.test_stale_reason_is_cleared = function()
    g.server:exec(function()
        box.cfg{read_only = true, ro_reason = 'custom reason'}
        box.cfg{read_only = false}
        box.cfg{read_only = true}
    end)
    t.assert_equals(ro_reason(g.server), 'config')
end

-- ro_reason cannot be set without read_only = true.
g.test_ro_reason_requires_read_only = function()
    assert_cfg_error(g.server, {ro_reason = 'custom reason'},
                     'may be set only when read_only is true')
end

-- ro_reason cannot be set when read_only = false.
g.test_ro_reason_rejects_read_only_false = function()
    assert_cfg_error(g.server, {
        read_only = false,
        ro_reason = 'custom reason',
    }, 'read_only is true')
end

-- Custom reason is carried in the ER_READONLY error.
g.test_custom_reason_in_error = function()
    local err = g.server:exec(function()
        box.cfg{read_only = true, ro_reason = 'custom reason'}
        local _, err = pcall(box.schema.create_space, 'test')
        return err:unpack()
    end)
    t.assert_covers(err, {
        reason = 'custom reason',
        code = box.error.READONLY,
        type = 'ClientError',
    })
    t.assert_str_contains(err.message,
                          'box.cfg.read_only is true - custom reason')
end

-- An over-long reason is rejected instead of being silently truncated.
g.test_ro_reason_too_long_is_rejected = function()
    assert_cfg_error(g.server, {
        read_only = true,
        ro_reason = string.rep('x', 512),
    }, 'must not exceed')
end

-- The largest accepted reason survives the round-trip without truncation.
g.test_ro_reason_max_len_round_trip = function()
    local len = g.server:exec(function()
        local reason = string.rep('x', 511)
        box.cfg{read_only = true, ro_reason = reason}
        return #box.info.ro_reason
    end)
    t.assert_equals(len, 511)
end

-- Special characters are kept verbatim in box.cfg/box.info but escaped in the
-- ER_READONLY message, so it stays a single line.
g.test_special_chars_are_escaped_in_error = function()
    local raw = 'tab\there\nnew"quote"\\slash\r\b'
    local cfg_reason, err = g.server:exec(function(raw)
        box.cfg{read_only = true, ro_reason = raw}
        local _, err = pcall(box.schema.create_space, 'test')
        return box.cfg.ro_reason, err:unpack()
    end, {raw})
    -- The original value is preserved as is.
    t.assert_equals(cfg_reason, raw)
    t.assert_equals(ro_reason(g.server), raw)
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

-- The higher-priority 'synchro' reason is returned while a custom reason is
-- configured and read_only remains true.
g2.test_synchro_overrides_custom_reason = function()
    g2.master:exec(function()
        box.cfg{election_mode = 'candidate', replication_synchro_quorum = 2}
    end)
    g2.replica:exec(function()
        box.cfg{read_only = true, ro_reason = 'custom reason'}
        box.cfg{election_mode = 'voter'}
    end)
    g2.master:wait_for_election_leader()
    g2.replica:wait_until_election_leader_found()

    t.assert_equals(g2.replica:exec(function()
        return box.cfg.read_only
    end), true)
    t.assert_equals(ro_reason(g2.replica), 'synchro')
end

-- read_only is false, but the instance is RO because it is an orphan, so the
-- 'orphan' reason is returned.
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
    t.assert_equals(ro_reason(g2.master), 'orphan')
end
