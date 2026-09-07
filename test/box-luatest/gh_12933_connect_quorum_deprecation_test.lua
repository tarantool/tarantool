local json = require('json')
local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

local QUORUM_WARNING = 'Deprecated option replication_connect_quorum, ' ..
                       'please use bootstrap_strategy instead'

local function run_script(script, env)
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', script)
    local res = justrun.tarantool(dir, env or {}, {'main.lua'},
                                  {nojson = true, stderr = true})
    t.assert_equals(res.exit_code, 0, res.stderr)
    return res
end

local function run(box_cfg, env)
    return run_script(([[
        box.cfg(require('json').decode('%s'))
        print(box.cfg.bootstrap_strategy)
        os.exit(0)
    ]]):format(json.encode(box_cfg)), env)
end

g.test_no_warning_with_legacy_from_cfg = function()
    local res = run({
        replication_connect_quorum = 1,
        bootstrap_strategy = 'legacy',
    })
    t.assert_not_str_contains(res.stderr, QUORUM_WARNING)
    t.assert_equals(res.stdout, 'legacy')
end

g.test_warning_with_auto_from_cfg = function()
    local res = run({
        replication_connect_quorum = 1,
        bootstrap_strategy = 'auto',
    })
    t.assert_str_contains(res.stderr, QUORUM_WARNING)
    t.assert_equals(res.stdout, 'auto')
end

g.test_warning_with_quorum_alone = function()
    local res = run({replication_connect_quorum = 1})
    t.assert_str_contains(res.stderr, QUORUM_WARNING)
    t.assert_equals(res.stdout, 'legacy')
end

g.test_no_warning_with_legacy_from_env = function()
    local res = run({replication_connect_quorum = 1},
                    {TT_BOOTSTRAP_STRATEGY = 'legacy'})
    t.assert_not_str_contains(res.stderr, QUORUM_WARNING)
    t.assert_equals(res.stdout, 'legacy')
end

g.test_warning_with_auto_from_env = function()
    local res = run({replication_connect_quorum = 1},
                    {TT_BOOTSTRAP_STRATEGY = 'auto'})
    t.assert_str_contains(res.stderr, QUORUM_WARNING)
    t.assert_equals(res.stdout, 'auto')
end

g.test_no_warning_with_all_from_env = function()
    local res = run({}, {
        TT_REPLICATION_CONNECT_QUORUM = '1',
        TT_BOOTSTRAP_STRATEGY = 'legacy',
    })
    t.assert_not_str_contains(res.stderr, QUORUM_WARNING)
    t.assert_equals(res.stdout, 'legacy')
end

-- The suppression works only in the scope of a single box.cfg call. The
-- strategy chosen on a previous configuration does not count.
g.test_warning_with_legacy_from_previous_cfg = function()
    local res = run_script([[
        box.cfg{bootstrap_strategy = 'legacy'}
        box.cfg{replication_connect_quorum = 0}
        os.exit(0)
    ]])
    t.assert_str_contains(res.stderr, QUORUM_WARNING)
end
