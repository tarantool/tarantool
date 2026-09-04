local json = require('json')
local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

local QUORUM_WARNING = 'Deprecated option replication_connect_quorum, ' ..
                       'please use bootstrap_strategy instead'

local OVERRIDE_ERROR = 'can not override a value for a deprecated option'

-- Run a fresh Tarantool with the given box.cfg argument, env
-- variables and extra CLI options. The instance prints the
-- options involved into the deprecated option translation after
-- the box.cfg call.
local function run(box_cfg, env, cli_opts)
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', ([[
        local json = require('json')
        box.cfg(%s)
        print(json.encode({
            bootstrap_strategy = box.cfg.bootstrap_strategy,
            replication_connect_quorum = box.cfg.replication_connect_quorum,
            force_recovery = box.cfg.force_recovery,
        }))
        os.exit(0)
    ]]):format(box_cfg))
    local args = table.copy(cli_opts or {})
    table.insert(args, 'main.lua')
    local res = justrun.tarantool(dir, env, args,
                                  {nojson = true, stderr = true})
    local cfg
    if res.exit_code == 0 then
        cfg = json.decode(res.stdout)
    end
    return res, cfg
end

g.test_env_deprecated_option_is_translated = function()
    local res, cfg = run('{}', {TT_REPLICATION_CONNECT_QUORUM = '1'})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_equals(cfg.bootstrap_strategy, 'legacy')
    t.assert_equals(cfg.replication_connect_quorum, 1)
    t.assert_str_contains(res.stderr, QUORUM_WARNING)
end

g.test_env_new_option_is_not_overridden = function()
    local res, cfg = run('{replication_connect_quorum = 1}',
                         {TT_BOOTSTRAP_STRATEGY = 'auto'})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_equals(cfg.bootstrap_strategy, 'auto')
    t.assert_equals(cfg.replication_connect_quorum, 1)
    t.assert_str_contains(res.stderr, QUORUM_WARNING)
end

g.test_env_both_options = function()
    local res, cfg = run('{}', {
        TT_REPLICATION_CONNECT_QUORUM = '1',
        TT_BOOTSTRAP_STRATEGY = 'auto',
    })
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_equals(cfg.bootstrap_strategy, 'auto')
    t.assert_equals(cfg.replication_connect_quorum, 1)
    t.assert_str_contains(res.stderr, QUORUM_WARNING)
end

g.test_env_conflict_is_rejected = function()
    local res = run('{panic_on_wal_error = true}', {TT_FORCE_RECOVERY = 'true'})
    t.assert_not_equals(res.exit_code, 0)
    t.assert_str_contains(res.stderr, OVERRIDE_ERROR)
end

g.test_cli_conflict_is_rejected = function()
    local res = run('{panic_on_wal_error = true}', {}, {'--force-recovery'})
    t.assert_not_equals(res.exit_code, 0)
    t.assert_str_contains(res.stderr, OVERRIDE_ERROR)
end

g.test_cli_wins_over_env = function()
    local res, cfg = run('{}', {TT_FORCE_RECOVERY = 'false'},
                         {'--force-recovery'})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_equals(cfg.force_recovery, true)
end
