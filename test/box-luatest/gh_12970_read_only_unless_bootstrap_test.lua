local justrun = require('luatest.justrun')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local treegen = require('luatest.treegen')
local t = require('luatest')

local g = t.group()

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
        cg.server = nil
    end
    if cg.replica_set ~= nil then
        cg.replica_set:drop()
        cg.replica_set = nil
    end
    if cg.master ~= nil then
        cg.master:drop()
        cg.master = nil
    end
    if cg.replica ~= nil then
        cg.replica:drop()
        cg.replica = nil
    end
end)

-- An instance bootstrapping a new replicaset with read_only =
-- 'unless_bootstrap' goes RW; the same instance recovering from the
-- local snapshot goes RO with no transient RW state. Note that the
-- value left in the configuration does not fail the restart.
g.test_bootstrap_rw_recovery_ro = function(cg)
    cg.server = server:new({box_cfg = {read_only = 'unless_bootstrap'}})
    cg.server:start()
    cg.server:exec(function()
        t.assert_equals(box.cfg.read_only, 'unless_bootstrap')
        t.assert_equals(box.info.ro, false)
        t.assert_equals(box.internal.is_bootstrap_leader(), true)
    end)
    t.assert(cg.server:grep_log('box switched to rw'))

    cg.server:restart()
    cg.server:exec(function()
        t.assert_equals(box.cfg.read_only, 'unless_bootstrap')
        t.assert_equals(box.info.ro, true)
        t.assert_equals(box.info.ro_reason, 'config')
        t.assert_equals(box.internal.is_bootstrap_leader(), false)
        local ok = pcall(box.ctl.wait_rw, 0.1)
        t.assert_not(ok)
    end)
    -- grep_log() with the default reset option looks only after the
    -- last restart banner: the recovered instance must not go through
    -- a transient RW state.
    t.assert_not(cg.server:grep_log('box switched to rw'))
    -- A positive control that the log is being read.
    t.assert(cg.server:grep_log('entering the event loop'))
end

-- An instance joining an existing replicaset with read_only =
-- 'unless_bootstrap' goes RO with no transient RW state.
g.test_join_is_ro = function(cg)
    cg.master = server:new({alias = 'master'})
    cg.master:start()
    cg.replica = server:new({
        alias = 'replica',
        box_cfg = {
            read_only = 'unless_bootstrap',
            replication = {cg.master.net_box_uri},
        },
    })
    cg.replica:start()
    cg.replica:exec(function()
        t.assert_equals(box.cfg.read_only, 'unless_bootstrap')
        t.assert_equals(box.info.ro, true)
        t.assert_equals(box.internal.is_bootstrap_leader(), false)
    end)
    t.assert_not(cg.replica:grep_log('box switched to rw'))
    t.assert(cg.replica:grep_log('entering the event loop'))
end

-- In the bootstrap master scoring the 'unless_bootstrap' mode counts
-- as "the configuration does not enforce RO": such an instance wins
-- the bootstrap over a read_only = true one.
g.test_wins_bootstrap_scoring = function(cg)
    cg.replica_set = replica_set:new({})
    local uri_ub = server.build_listen_uri('ub', cg.replica_set.id)
    local uri_ro = server.build_listen_uri('ro', cg.replica_set.id)
    local replication = {uri_ub, uri_ro}
    cg.replica_set:build_and_add_server({
        alias = 'ub',
        box_cfg = {
            read_only = 'unless_bootstrap',
            replication = replication,
        },
    })
    cg.replica_set:build_and_add_server({
        alias = 'ro',
        box_cfg = {
            read_only = true,
            replication = replication,
        },
    })
    cg.replica_set:start()
    cg.replica_set:get_server('ub'):exec(function()
        t.assert_equals(box.info.ro, false)
        t.assert_equals(box.internal.is_bootstrap_leader(), true)
    end)
    cg.replica_set:get_server('ro'):exec(function()
        t.assert_equals(box.info.ro, true)
        t.assert_equals(box.internal.is_bootstrap_leader(), false)
    end)
end

-- The box.status watcher reports is_ro_cfg the same way as the
-- ballot: whether the configuration unconditionally enforces RO. The
-- 'unless_bootstrap' value is not an unconditional RO enforcement;
-- the effective state is visible in is_ro.
g.test_status_watcher = function(cg)
    cg.server = server:new({box_cfg = {read_only = 'unless_bootstrap'}})
    cg.server:start()
    local check_status = function(expected_is_ro)
        local status
        local w = box.watch('box.status', function(_, value)
            status = value
        end)
        t.helpers.retrying({timeout = 5}, function()
            t.assert_not_equals(status, nil)
        end)
        w:unregister()
        t.assert_equals(status.is_ro_cfg, false)
        t.assert_equals(status.is_ro, expected_is_ro)
    end
    cg.server:exec(check_status, {false})
    cg.server:restart()
    cg.server:exec(check_status, {true})
end

-- The 'unless_bootstrap' value may be set only during the initial
-- box.cfg() call: the outcome of a dynamic switch would be
-- unpredictable when a leader is already chosen elsewhere.
g.test_dynamic_set_is_forbidden = function(cg)
    cg.server = server:new()
    cg.server:start()
    local check_banned = function()
        t.assert_error_msg_contains(
            "the 'unless_bootstrap' value may be set only during " ..
            "the initial box.cfg() call",
            box.cfg, {read_only = 'unless_bootstrap'})
        t.assert_equals(box.cfg.read_only, false)
        t.assert_equals(box.info.ro, false)
    end
    -- Banned right after the bootstrap...
    cg.server:exec(check_banned)
    -- ...and after a restart as well.
    cg.server:restart()
    cg.server:exec(check_banned)
end

-- Invalid values are rejected.
g.test_invalid_value = function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        t.assert_error_msg_contains(
            "the value must be a boolean or the string 'unless_bootstrap'",
            box.cfg, {read_only = 'rw'})
        t.assert_error_msg_contains(
            'should be one of types boolean, string',
            box.cfg, {read_only = 1})
        -- The configuration is intact.
        t.assert_equals(box.cfg.read_only, false)
    end)
end

-- An invalid value fails the initial box.cfg() call: the instance
-- refuses to start.
g.test_invalid_value_on_startup = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', [[
        box.cfg({read_only = 'blah'})
        os.exit(0)
    ]])
    local res = justrun.tarantool(dir, {}, {'main.lua'},
        {nojson = true, stderr = true})
    t.assert_not_equals(res.exit_code, 0)
    t.assert_str_contains(res.stderr,
        "Incorrect value for option 'read_only': " ..
        "the value must be a boolean or the string 'unless_bootstrap'")
end

-- 'unless_bootstrap' is not allowed together with replication_anon
-- (an anonymous replica must be strictly read-only).
g.test_incompatible_with_anon = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', [[
        box.cfg({
            read_only = 'unless_bootstrap',
            replication_anon = true,
        })
        os.exit(0)
    ]])
    local res = justrun.tarantool(dir, {}, {'main.lua'},
        {nojson = true, stderr = true})
    t.assert_not_equals(res.exit_code, 0)
    t.assert_str_contains(res.stderr,
        'the value may be set to true only when the instance is read-only')
end

-- TT_READ_ONLY environment variable accepts 'unless_bootstrap' along
-- with the usual booleans.
g.test_env_tt_read_only = function(cg)
    cg.server = server:new({env = {TT_READ_ONLY = 'unless_bootstrap'}})
    cg.server:start()
    cg.server:exec(function()
        t.assert_equals(box.cfg.read_only, 'unless_bootstrap')
        t.assert_equals(box.info.ro, false)
    end)
    cg.server:drop()

    cg.server = server:new({env = {TT_READ_ONLY = 'false'}})
    cg.server:start()
    cg.server:exec(function()
        t.assert_equals(box.cfg.read_only, false)
        t.assert_equals(box.info.ro, false)
    end)
end
