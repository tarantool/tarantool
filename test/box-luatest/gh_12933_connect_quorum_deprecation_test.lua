local server = require('luatest.server')
local t = require('luatest')

local WARNING_PATTERN = 'Deprecated option replication_connect_quorum, ' ..
                        'please use bootstrap_strategy instead'

local g = t.group()

local function server_start(cg, box_cfg, env)
    env = env or {}
    -- On the initial box.cfg call the deprecation warning is
    -- logged before the logger is configured, so it lands into
    -- stderr rather than the log file. Set the logger up in
    -- advance to be able to find the warning in the file.
    env.TARANTOOL_RUN_BEFORE_BOX_CFG = [[
        require('log').cfg{
            log = os.getenv('TARANTOOL_WORKDIR') .. '/' ..
                  os.getenv('TARANTOOL_ALIAS') .. '.log',
        }
    ]]
    cg.server = server:new{
        box_cfg = box_cfg,
        env = env,
    }
    cg.server:start()
end

-- Server:grep_log() can not be used here - it ignores everything
-- logged before the Tarantool version banner, and the warning of
-- the initial box.cfg call is logged before the banner.
local function count_warnings(cg)
    return cg.server:exec(function(pattern)
        local fio = require('fio')
        local log_file = rawget(_G, 'box_cfg_log_file') or box.cfg.log
        local f = assert(fio.open(log_file, {'O_RDONLY'}))
        local data = f:read(f:stat().size)
        f:close()
        local count = 0
        for _ in data:gmatch(pattern) do
            count = count + 1
        end
        return count
    end, {WARNING_PATTERN})
end

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

g.test_no_warning_with_explicit_legacy = function(cg)
    server_start(cg, {
        replication_connect_quorum = 1,
        bootstrap_strategy = 'legacy',
    })
    t.assert_equals(count_warnings(cg), 0)

    cg.server:exec(function() box.cfg{replication_connect_quorum = 2} end)
    t.assert_equals(count_warnings(cg), 0)
end

g.test_no_warning_with_env_legacy = function(cg)
    server_start(cg, {replication_connect_quorum = 1},
                 {TT_BOOTSTRAP_STRATEGY = 'legacy'})
    t.assert_equals(count_warnings(cg), 0)
    t.assert_equals(cg.server:get_box_cfg().bootstrap_strategy, 'legacy')
end

g.test_warning_with_env_auto = function(cg)
    server_start(cg, {replication_connect_quorum = 1},
                 {TT_BOOTSTRAP_STRATEGY = 'auto'})
    t.assert_equals(count_warnings(cg), 1)
end

g.test_warning_without_legacy = function(cg)
    server_start(cg, {
        replication_connect_quorum = 1,
        bootstrap_strategy = 'auto',
    })
    t.assert_equals(count_warnings(cg), 1)
    t.assert_equals(cg.server:get_box_cfg().bootstrap_strategy, 'auto')
end

g.test_warning_on_reload_without_legacy = function(cg)
    server_start(cg)
    t.assert_equals(count_warnings(cg), 0)

    cg.server:exec(function() box.cfg{replication_connect_quorum = 1} end)
    t.assert_equals(count_warnings(cg), 1)
    t.assert_equals(cg.server:get_box_cfg().bootstrap_strategy, 'legacy')
end

g.test_warning_once_on_implicit_legacy = function(cg)
    server_start(cg, {replication_connect_quorum = 1})
    t.assert_equals(count_warnings(cg), 1)
    t.assert_equals(cg.server:get_box_cfg().bootstrap_strategy, 'legacy')

    cg.server:exec(function() box.cfg{replication_connect_quorum = 2} end)
    t.assert_equals(count_warnings(cg), 1)
end
