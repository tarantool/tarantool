local server = require('luatest.server')
local t = require('luatest')

local WARNING_PATTERN = 'W> Option wal_cleanup_delay is deprecated.'

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_wal_cleanup_delay_deprecation = function(cg)
    t.assert_not(cg.server:grep_log(WARNING_PATTERN),
                 'No deprecation warning in the log initially')

    local run_before_cfg =
        "require('compat').wal_cleanup_delay_deprecation = 'old'"
    local env = {
        ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = run_before_cfg,
    }
    cg.server:restart{env = env}
    t.assert_not(cg.server:grep_log(WARNING_PATTERN),
                 'No deprecation warning in the log after restart')
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, nil)

    cg.server:exec(function() box.cfg{wal_cleanup_delay = 4 * 3600} end)
    t.assert(cg.server:grep_log(WARNING_PATTERN),
             'Deprecation warning in the log when setting default value')
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, 4 * 3600)

    cg.server:exec(function() box.cfg{wal_cleanup_delay = 0} end)
    t.assert(cg.server:grep_log(WARNING_PATTERN),
             'Deprecation warning in the log when setting zero value')
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, 0)

    cg.server:exec(function() box.cfg{wal_cleanup_delay = 10} end)
    t.assert(cg.server:grep_log(WARNING_PATTERN),
             'Deprecation warning in the log when setting custom value')
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, 10)

    run_before_cfg = "require('compat').wal_cleanup_delay_deprecation = 'new'"
    env = {
        ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = run_before_cfg,
    }
    local errmsg = 'Option wal_cleanup_delay is deprecated'
    cg.server:restart{env = env}
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, nil)
    t.assert_error_msg_content_equals(
        errmsg, cg.server.exec, cg.server,
        function() box.cfg{wal_cleanup_delay = 4 * 3600} end)
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, nil)

    t.assert_error_msg_content_equals(
        errmsg, cg.server.exec, cg.server,
        function() box.cfg{wal_cleanup_delay = 0} end)
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, nil)

    t.assert_error_msg_content_equals(
        errmsg, cg.server.exec, cg.server,
        function() box.cfg{wal_cleanup_delay = 10} end)
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, nil)
end

-- Check if the deprecation check is done during initial `box.cfg` if the option
-- is passed and is not done if the option is not passed.
g.test_wal_cleanup_delay_deprecation_on_initial_configuration = function(cg)
    local run_before_cfg =
        "require('compat').wal_cleanup_delay_deprecation = 'old'"
    local env = {
        ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = run_before_cfg,
    }
    cg.server:restart{env = env}
    t.assert_not(cg.server:grep_log(WARNING_PATTERN))

    local box_cfg = {wal_cleanup_delay = 0}
    cg.server:restart{env = env, box_cfg = box_cfg}
    t.assert(cg.server:grep_log(WARNING_PATTERN))
end
