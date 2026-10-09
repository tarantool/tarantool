local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new({box_cfg = {enable_tracing = true}})
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_12998_check_call_back = function(cg)
    cg.server:exec(function()
        local function f1()
            require('fiber').sleep(0.1)
        end

        local function f2()
            local tracer = require('tracer')
            tracer:call_span("span_2", f1)
        end

        local tracer = require('tracer')
        tracer:call_span("span_1", f1)

        tracer:call_span("span_3", f2)
    end)
    t.assert(cg.server:grep_log('TRACER OPENTELEMETRY: span_id=.* name=span_1'))
    t.assert(cg.server:grep_log(
        'TRACER OPENTELEMETRY:.* parent_id=0000000000000002 .*name=span_2'
    ))
    t.assert(cg.server:grep_log('TRACER OPENTELEMETRY: span_id=.* name=span_3'))
end
