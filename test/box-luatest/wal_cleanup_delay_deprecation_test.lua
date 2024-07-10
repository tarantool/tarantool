local server = require('luatest.server')
local t = require('luatest')

local WARNING_PATTERN = 'W> Option wal_cleanup_delay is deprecated.'

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_wal_cleanup_delay_default = function(cg)
    t.assert_not(cg.server:grep_log(WARNING_PATTERN),
                 'No deprecation warning in the log initially')

    local run_before_cfg = "require('compat').box_cfg_wal_cleanup_delay = 'old'"
    local env = {
        ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = run_before_cfg,
    }
    cg.server:restart{env = env}
    t.assert_not(cg.server:grep_log(WARNING_PATTERN),
                 'No deprecation warning in the log after restart')
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, 4 * 3600)

    cg.server:exec(function() box.cfg{wal_cleanup_delay = 4 * 3600} end)
    t.assert_not(cg.server:grep_log(WARNING_PATTERN),
                 'No deprecation warning in the log when setting default value')
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, 4 * 3600)

    cg.server:exec(function() box.cfg{wal_cleanup_delay = 0} end)
    t.assert_not(cg.server:grep_log(WARNING_PATTERN),
                 'No deprecation warning in the log when setting zero value')
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, 0)

    cg.server:exec(function() box.cfg{wal_cleanup_delay = 10} end)
    t.assert(cg.server:grep_log(WARNING_PATTERN),
             'Deprecation warning in the log when setting custom value')
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, 10)

    run_before_cfg = "require('compat').box_cfg_wal_cleanup_delay = 'new'"
    env = {
        ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = run_before_cfg,
    }
    local errmsg = 'Option wal_cleanup_delay is deprecated'
    cg.server:restart{env = env}
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, 0)
    t.assert_error_msg_content_equals(
        errmsg, cg.server.exec, cg.server,
        function() box.cfg{wal_cleanup_delay = 4 * 3600} end)
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, 0)

    t.assert_error_msg_content_equals(
        errmsg, cg.server.exec, cg.server,
        function() box.cfg{wal_cleanup_delay = 10} end)
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, 0)

    cg.server:exec(function() box.cfg{wal_cleanup_delay = 0} end)
    t.assert_equals(cg.server:get_box_cfg().wal_cleanup_delay, 0)

    cg.server:exec(function()
        local compat = require('compat')
        local errmsg = "The compat  option 'box_cfg_wal_cleanup_delay' " ..
                       "takes effect only before the initial box.cfg() call"
        t.assert_error_msg_content_equals(errmsg,
            function() compat.box_cfg_wal_cleanup_delay = 'old' end)
    end)
end
