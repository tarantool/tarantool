local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_check_iterator_when_loading_gc_consumers = function(cg)
    local server_log_path = g.server:exec(function()
        return rawget(_G, 'box_cfg_log_file') or box.cfg.log
    end)

    -- Inject error after recovery - right before WAL GC consumers are loaded.
    local run_before_cfg = [[
        local trigger = require('trigger')
        trigger.set('box.ctl.on_recovery_state', 'errinj', function(state)
            if state == 'wal_recovered' then
                box.error.injection.set('ERRINJ_INDEX_ITERATOR_NEW', true)
            end
        end)
    ]]
    cg.server:restart({
        env = {
            ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = run_before_cfg,
        }
    }, {wait_until_ready = false})
    t.helpers.retrying({}, function()
        -- Check if the application panics.
        t.assert(g.server:grep_log("F> can't initialize storage", nil,
            {filename = server_log_path}))
        -- Check if loading WAL GC consumers is the reason.
        t.assert(g.server:grep_log("Failed to recover WAL GC consumers", nil,
            {filename = server_log_path}))
    end)
end
