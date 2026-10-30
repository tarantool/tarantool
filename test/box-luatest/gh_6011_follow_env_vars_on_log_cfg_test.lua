local server = require('luatest.server')
local fio = require('fio')
local t = require('luatest')

local g = t.group()

local TARANTOOL_PATH = arg[-1]

-- Test 'level' default value so that `test_env_set` success is not because
-- of 'warn' default.
local test_default = [[
    local log = require('log')
    log.cfg{}
    os.exit(log.cfg.level == 5 and 0 or 1)
]]

-- Main test. Test env vars are taken into account in log.cfg().
local test_env_set = [[
    local log = require('log')
    log.cfg{}
    os.exit(log.cfg.level == 'warn' and 0 or 1)
]]

-- Test we don't apply env vars again in box.cfg() after log.cfg for
-- log options.
local test_box_env_dont_overwrite = [[
    local log = require('log')
    log.cfg{level='error'}
    box.cfg{}
    os.exit(log.cfg.level == 'error' and 0 or 1)
]]

-- Test we don't fail in log.cfg() if non log options are specified
-- incorrectly in env vars.
local test_other_env_option_error = [[
    local log = require('log')
    log.cfg{level='error'}
    os.exit(log.cfg.level == 'error' and 0 or 1)
]]

-- Test all log options together.
local test_all_log_options = [[
    local log = require('log')
    log.cfg{}
    if log.cfg.log ~= 'test.log' or
        log.cfg.nonblock ~= false or
        log.cfg.level ~= 'error' or
        log.cfg.format ~= 'json' then
        os.exit(1)
    end
    box.cfg{}
    if box.cfg.worker_pool_threads ~= 2 or
        box.cfg.memtx_use_mvcc_engine ~= true then
        os.exit(2)
    end
    os.exit(0)
]]

local function run_test(g, test, env)
    local chdir_expr = string.format("require('fio').chdir('%s')", g.workdir)
    env = env or ""
    local cmd = string.format('%s %s -e "%s" -e "%s"',
                              env, TARANTOOL_PATH, chdir_expr, test)
    t.assert_equals(os.execute(cmd), 0)
end

g.test_env_vars_on_log_cfg = function()
    g.workdir = fio.pathjoin(server.vardir, 'gh-6011')
    fio.mkdir(g.workdir)

    run_test(g, test_default)
    run_test(g, test_env_set, 'TT_LOG_LEVEL=warn')
    run_test(g, test_box_env_dont_overwrite, 'TT_LOG_LEVEL=warn')
    run_test(g, test_other_env_option_error, 'TT_STRIP_CORE=a')

    -- FIXME (gh-8051)
    -- Don't test `TT_LOG_MODULES` option as it does not work yet gh-8051.
    local all_log_options = {
        'TT_LOG=test.log',
        'TT_LOG_NONBLOCK=false',
        'TT_LOG_LEVEL=error',
        'TT_LOG_FORMAT=json',
        'TT_WORKER_POOL_THREADS=2',
        'TT_MEMTX_USE_MVCC_ENGINE=true'
    }
    run_test(g, test_all_log_options, table.concat(all_log_options, ' '))

    fio.rmtree(g.workdir)
end
