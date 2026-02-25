local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_scheduler_unthrottle = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('primary')
        s:insert({1})
        box.error.injection.set('ERRINJ_VY_SCHED_TIMEOUT', 9000)
        box.error.injection.set('ERRINJ_VY_RUN_WRITE', true)
        t.assert_error_covers({
            type = 'ClientError',
            name = 'INJECTION',
            details = 'vinyl dump',
        }, box.snapshot)
        box.error.injection.set('ERRINJ_VY_RUN_WRITE', false)
        box.snapshot()
        s:drop()
    end)
end
