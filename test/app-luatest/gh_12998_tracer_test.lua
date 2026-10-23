local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_check_tracer = function(cg)
    cg.server:exec(function()
        local storage_span = {}
        rawset(_G, 'storage_span', storage_span)

        local function record_span(span)
            table.insert(_G.storage_span, span)
        end

        local function f1()
            require('fiber').sleep(0.1)
        end

        local function f2()
            local tracer = require('tracer')
            tracer.call_span("span_2", f1)
            return 1
        end

        local tracer = require('tracer')
        tracer.enable()
        tracer.on_span_end(record_span)
        tracer.call_span("span_1", f1)

        local res = tracer.call_span("span_3", f2)
        t.assert_equals(res, 1)

        tracer.disable()
        res = tracer.call_span("span_4", f2)
        t.assert_equals(res, 1)

        t.assert_equals(#_G.storage_span, 3)
        t.assert_equals(_G.storage_span[1].name, "span_1")
        t.assert_equals(_G.storage_span[2].name, "span_2")
        t.assert_equals(_G.storage_span[2].parent_id,
                        _G.storage_span[3].id)
        t.assert_equals(_G.storage_span[3].name, "span_3")
    end)
end

--
-- Verifying that the concurrent execution of different fibers does not
-- result in incorrect `parent_id` values.
--
g.test_check_tracer_with_fibers_parallel = function(cg)
    cg.server:exec(function()
        local storage_span = {}
        rawset(_G, 'storage_span', storage_span)

        local function record_span(span)
            table.insert(_G.storage_span, span)
        end

        local function f1()
            require('fiber').sleep(0.1)
        end

        local fiber = require('fiber')

        local function s1()
            local tracer = require("tracer")
            tracer.enable()
            tracer.call_span("span_fiber_1", f1)
        end

        local function s2()
            local tracer = require("tracer")
            tracer.enable()
            tracer.call_span("span_fiber_2", f1)
        end

        local tracer = require('tracer')
        tracer.enable()
        tracer.on_span_end(record_span)

        local fiber1 = fiber.new(s1)
        local fiber2 = fiber.new(s2)
        fiber.set_joinable(fiber1, true)
        fiber.set_joinable(fiber2, true)
        fiber.join(fiber1)
        fiber.join(fiber2)
        t.assert_equals(#_G.storage_span, 2)
        t.assert_equals(_G.storage_span[1].parent_id, 0)
        t.assert_equals(_G.storage_span[2].parent_id, 0)
    end)
end

--
-- Verify that `parent_id` is not lost
-- when there are multiple nested fiber calls.
--
g.test_check_tracer_with_fibers_successive = function(cg)
    cg.server:exec(function()
        local storage_span = {}
        rawset(_G, 'storage_span', storage_span)

        local function record_span(span)
            table.insert(_G.storage_span, span)
        end

        local function f1()
            require('fiber').sleep(0.1)
        end

        local function f2()
            local fiber = require("fiber")
            fiber.new(f1)
        end

        local function f3()
            local fiber = require("fiber")
            fiber.new(f2)
        end

        local function s()
            local tracer = require("tracer")
            tracer.enable()
            tracer.call_span("span_fiber_3", f3)
        end

        local tracer = require('tracer')
        tracer.enable()
        tracer.on_span_end(record_span)

        tracer.call_span("span_fiber_4", s)
        t.assert_equals(#_G.storage_span, 2)
        t.assert_equals(_G.storage_span[1].parent_id, _G.storage_span[2].id)
    end)
end
