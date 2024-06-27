local t = require('luatest')
local server = require('luatest.server')

local g = t.group('deprecate_replication_synchro_timeout')
--
-- gh-7486: deprecate `replication_synchro_timeout`.
--

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_option_available_if_old = function(cg)
    cg.server = server:new({alias = 'old_behavior'})
    cg.server:start()
    cg.server:exec(function()
        t.assert_equals(box.cfg.replication_synchro_timeout, 5)
        local ok, _ = pcall(box.cfg, {replication_synchro_timeout = 239})
        t.assert(ok)
        t.assert_equals(box.cfg.replication_synchro_timeout, 239)
    end)
end

g.test_option_not_available_if_new = function(cg)
    cg.server = server:new({
        alias = 'new_behavior',
        env = {
            TARANTOOL_RUN_BEFORE_BOX_CFG = [[
                require('compat').box_cfg_replication_synchro_timeout = 'new'
            ]]
        }
    })
    cg.server:start()
    cg.server:exec(function()
        t.assert_equals(box.cfg.replication_synchro_timeout, box.NULL)
        local ok, err = pcall(box.cfg, {replication_synchro_timeout = 5})
        t.assert(not ok)
        t.assert_equals(err.type, 'ClientError')
        t.assert_equals(err.message, "Option 'replication_synchro_timeout' " ..
            "is deprecated")
    end)
end
