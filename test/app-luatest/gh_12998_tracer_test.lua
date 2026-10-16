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

        local function record_span(span)
            table.insert(storage_span, span)
        end

        local function f1()
            require('fiber').sleep(0.1)
        end

        local function f2()
            require("tracer").call_span("span_2", f1)
            return 1
        end

        local tracer = require('tracer')
        tracer.set_trace_probability(1)
        tracer.on_span_end(record_span)
        tracer.call_span("span_1", f1)

        local res = tracer.call_span("span_3", f2)
        t.assert_equals(res, 1)

        tracer.set_trace_probability(0)
        res = tracer.call_span("span_4", f2)
        t.assert_equals(res, 1)

        tracer.set_trace_probability(1)
        tracer.on_span_end(nil)
        tracer.call_span("span_5", f1)

        t.assert_equals(#storage_span, 3)
        t.assert_equals(storage_span[1].name, "span_1")
        t.assert_equals(storage_span[2].name, "span_2")
        t.assert_equals(storage_span[2].parent_id,
                        storage_span[3].id)
        t.assert_equals(storage_span[3].name, "span_3")
    end)
end

g.test_check_set_get_probability = function(cg)
    cg.server:exec(function()
        local tracer = require('tracer')

        local p = 0.6
        tracer.set_trace_probability(p)
        t.assert_equals(tracer.get_trace_probability(), p)
    end)
end

g.test_function_with_error = function(cg)
    cg.server:exec(function()
        local storage_span = {}

        local function record_span(span)
            table.insert(storage_span, span)
        end

        local function f_err()
            error("some error", 0)
        end

        local function f_err2()
            box.error(box.error.ILLEGAL_PARAMS, 'test')
        end

        local tracer = require('tracer')
        tracer.on_span_end(record_span)

        tracer.set_trace_probability(1)
        local exp_err = "some error"
        local _, err = pcall(tracer.call_span, "span_1", f_err)
        t.assert_equals(err, exp_err)

        local exp_err2 = "test"
        _, err = pcall(tracer.call_span, 'span_2', f_err2)
        t.assert_equals(box.error.is(err), true)
        t.assert_equals(err.message, exp_err2)

        tracer.set_trace_probability(0)
        _, err = pcall(tracer.call_span, "span_3", f_err)
        t.assert_equals(err, exp_err)

        _, err = pcall(tracer.call_span, 'span_4', f_err2)
        t.assert_equals(box.error.is(err), true)
        t.assert_equals(err.message, exp_err2)

        t.assert_equals(#storage_span, 2)
        t.assert_equals(storage_span[1].name, "span_1")
        t.assert_equals(storage_span[2].name, "span_2")
    end)
end

g.test_error_during_record_span = function(cg)
    cg.server:exec(function()
        local function record_span(span)
            error("some error", 0)
        end

        local function f1()
            require('fiber').sleep(0.1)
        end

        local tracer = require('tracer')
        tracer.on_span_end(record_span)

        tracer.set_trace_probability(1)
        local exp_err = "some error"
        tracer.call_span("span", f1)
    end)
    t.assert(cg.server:grep_log("tracer: error during record span: some error"))
end

--
-- Verify that multiple arguments can be returned.
--
g.test_function_with_many_arguments = function(cg)
    cg.server:exec(function()
        local storage_span = {}

        local function record_span(span)
            table.insert(storage_span, span)
        end

        local function f1(...)
            return select('#', ...), ...
        end

        local tracer = require('tracer')
        tracer.set_trace_probability(1)
        tracer.on_span_end(record_span)

        local res1, res2, res3, res4 = tracer.call_span("span_1", f1, 1, nil, 2)
        t.assert_equals(res1, 3)
        t.assert_equals(res2, 1)
        t.assert_equals(res3, nil)
        t.assert_equals(res4, 2)

        tracer.set_trace_probability(0)
        res1, res2, res3, res4 = tracer.call_span("span_2", f1, 1, nil, 2)
        t.assert_equals(res1, 3)
        t.assert_equals(res2, 1)
        t.assert_equals(res3, nil)
        t.assert_equals(res4, 2)

        t.assert_equals(#storage_span, 1)
        t.assert_equals(storage_span[1].name, "span_1")
    end)
end

--
-- Check that decision to record spans is made ONLY in the root span.
--
g.test_decision_made_only_in_root_span = function(cg)
    cg.server:exec(function()
        local storage_span = {}

        local function record_span(span)
            table.insert(storage_span, span)
        end

        local function f1()
            require('fiber').sleep(0.1)
        end

        local function f2()
            local tracer = require('tracer')
            tracer.set_trace_probability(1)
            tracer.call_span("span_f1", f1)
        end

        local function f3()
            local fiber = require('fiber')
            local fib = fiber.new(f2)
            fiber.set_joinable(fib, true)
            fiber.join(fib)
        end

        local tracer = require('tracer')
        tracer.on_span_end(record_span)

        tracer.call_span("span_f3", f3)

        t.assert_equals(#storage_span, 0)
    end)
end

--
-- Check error messages about wrong arguments in main function of tracer.
--
g.test_wrong_arguments = function(cg)
    cg.server:exec(function()
        local tracer = require('tracer')

        t.assert_error_msg_contains("probability should be a number",
                                    tracer.set_trace_probability, "abs")
        t.assert_error_msg_contains("probability must be between 0 and 1",
                                    tracer.set_trace_probability, -10)

        t.assert_error_msg_contains("func should be a function",
                                    tracer.on_span_end, "asd")
        t.assert_error_msg_contains("name should be a string",
                                    tracer.call_span, 0, "asd")
        t.assert_error_msg_contains("func should be a function",
                                    tracer.call_span, "span", "asd")
    end)
end

--
-- Verifying that the concurrent execution of different fibers does not
-- result in incorrect `parent_id` values.
--
g.test_check_tracer_with_fibers_parallel = function(cg)
    cg.server:exec(function()
        local storage_span = {}

        local function record_span(span)
            table.insert(storage_span, span)
        end

        local function f1()
            require('fiber').sleep(0.1)
        end

        local fiber = require('fiber')

        local function s1()
            require("tracer").call_span("span_fiber_1", f1)
        end

        local function s2()
            require("tracer").call_span("span_fiber_2", f1)
        end

        local tracer = require('tracer')
        tracer.set_trace_probability(1)
        tracer.on_span_end(record_span)

        local fiber1 = fiber.new(s1)
        local fiber2 = fiber.new(s2)
        fiber.set_joinable(fiber1, true)
        fiber.set_joinable(fiber2, true)
        fiber.join(fiber1)
        fiber.join(fiber2)
        t.assert_equals(#storage_span, 2)
        t.assert_equals(storage_span[1].parent_id, 0)
        t.assert_equals(storage_span[2].parent_id, 0)
    end)
end

--
-- Verify that `parent_id` is not lost
-- when there are multiple nested fiber calls.
--
g.test_check_tracer_with_fibers_successive = function(cg)
    cg.server:exec(function()
        local storage_span = {}

        local function record_span(span)
            table.insert(storage_span, span)
        end

        local function f0()
            require('fiber').sleep(0.1)
        end

        local function f1()
            require("tracer").call_span("span_fiber_3", f0)
        end

        local function f2()
            local fiber = require('fiber')
            local fib = fiber.new(f1)
            fiber.set_joinable(fib, true)
            fiber.join(fib)
        end

        local function f3()
            local fiber = require('fiber')
            local fib = fiber.new(f2)
            fiber.set_joinable(fib, true)
            fiber.join(fib)
        end

        local function s()
            local fiber = require('fiber')
            local fib = fiber.new(f3)
            fiber.set_joinable(fib, true)
            fiber.join(fib)
        end

        local tracer = require('tracer')
        tracer.set_trace_probability(1)
        tracer.on_span_end(record_span)

        tracer.call_span("span_fiber_4", s)
        t.assert_equals(#storage_span, 2)
        t.assert_equals(storage_span[1].parent_id, storage_span[2].id)
    end)
end

--
-- Verify that `parent_id` is not lost in iproto calls.
--
g.test_check_tracer_with_iproto = function(cg)
    cg.server:exec(function()
        local storage_span = {}
        rawset(_G, 'storage_span', storage_span)

        local function record_span(span)
            table.insert(_G.storage_span, span)
        end

        box.schema.user.create('test_user', {password = 'test'})
        box.schema.user.grant('test_user', 'read, write, execute', 'universe')
        box.schema.user.grant('test_user', 'create', 'space')

        local function f2()
            box.schema.func.create('f1', {
                if_not_exists = true,
                body = [[
                    function()
                        local tracer = require('tracer')
                        tracer.call_span("span_f0", function() return 0 end)
                    end
                ]]
            })
            box.schema.func.create('f1', {if_not_exists = true})
            local net_box = require('net.box')
            local cn = net_box.connect(box.cfg.listen,
                                       {user = 'test_user', password = 'test'})
            cn:call('f1', {})
        end

        local tracer = require('tracer')
        tracer.set_trace_probability(1)
        tracer.on_span_end(record_span)

        tracer.call_span("span_f2", f2)

        local function f3()
            rawset(_G, 'f0', function() return 0 end)
            local net_box = require('net.box')
            local cn = net_box.connect(box.cfg.listen,
                                       {user = 'test_user',
                                        password = 'test'})
            local eval_str = 'require("tracer").call_span("span_f00", f0)'
            cn:eval(eval_str, {})
        end
        tracer.call_span("span_f3", f3)
        t.assert_equals(#_G.storage_span, 4)
        t.assert_equals(_G.storage_span[1].name, "span_f0")
        t.assert_equals(_G.storage_span[2].name, "span_f2")
        t.assert_equals(_G.storage_span[1].parent_id, _G.storage_span[2].id)
        t.assert_equals(_G.storage_span[3].name, "span_f00")
        t.assert_equals(_G.storage_span[4].name, "span_f3")
        t.assert_equals(_G.storage_span[3].parent_id, _G.storage_span[4].id)
    end)
end

local g_app = t.group("app_thread")

g_app.before_all(function(cg)
    cg.server = server:new({
        box_cfg = {app_threads = 1},
        net_box_credentials = {user = 'admin'},
    })
    cg.server:start()
end)

g_app.after_all(function(cg)
    cg.server:drop()
end)

g_app.test_works_in_an_application_thread = function(cg)
    local storage_span = cg.server:exec(function()
        local storage_span = {}

        local function record_span(span)
            table.insert(storage_span, span)
        end

        local function f0()
            require('fiber').sleep(0.1)
        end

        local tracer = require('tracer')
        tracer.set_trace_probability(1)
        tracer.on_span_end(record_span)

        tracer.call_span("span", f0)
        return storage_span
    end, {}, {_thread_id = 1})
    t.assert_equals(#storage_span, 1)
    t.assert_equals(storage_span[1].name, "span")
end
