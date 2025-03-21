local t = require('luatest')
local g = t.group('Feedback daemon metrics')
local server = require('luatest.server')

g.before_all(function()
    g.server = server:new{
        alias = 'default',
        box_cfg = {
            metrics = {include = 'none'},
            -- The tests here are unit ones, works with dummy metrics
            -- package. After metrics package embedding, feedback background
            -- loop may succeed to extract empty metrics table from
            -- the embedded package, causing t.assert_equals(fb.metrics, nil)
            -- to fail.
            feedback_send_metrics = false,
        }
    }
    g.server:start()
    g.server:exec(function()
        require('third_party.metrics.test.rock_utils').remove_builtin('metrics')
        box.cfg{feedback_send_metrics = true}
    end)
end)

g.after_all(function()
    g.server:drop()
end)

g.before_each(function()
    g.server:exec(function()
        -- Replace module metrics with empty table to avoid metrics collection.
        package.loaded["metrics"] = {}
        -- Daemon is reloaded when cfg option is updated.
        box.cfg{
            feedback_send_metrics = true,
            feedback_metrics_collect_interval = 0.01,
            feedback_metrics_limit = 1024 * 1024
        }
    end)
end)

g.test_metrics = function()
    g.server:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local m = {}
        -- Method checks that we pass required arguments to collect defaults.
        -- Note that if one of assertions in the method fails, you will not
        -- see it directly - the method is called in internal fiber. Metrics
        -- just will not be collected and metrics checkers will fail.
        m.collect = function(args)
            t.assert_type(args, 'table')
            t.assert_equals(args.invoke_callbacks, true)
            t.assert_equals(args.default_only, true)
            return {1, 2}
        end
        m._VERSION = "0.16.0"
        package.loaded["metrics"] = m
        local start = fiber.time()
        fiber.sleep(0.5)
        local daemon = box.internal.feedback_daemon
        local fb = daemon.generate_feedback()
        -- This check may fail because of assertion failure in collect method.
        t.assert_type(fb.metrics, 'table')
        -- This check verifies that *some* samples are collected and that there
        -- are not too many of them. It doesn't attempt to ensure correct
        -- timings, which is nearly impossible in the general case, when many
        -- processes performs its own workloads in parallel.
        t.assert_gt(#fb.metrics, 0)
        local elapsed = fiber.time() - start
        t.assert_lt(#fb.metrics,
                    1.1 * elapsed / box.cfg.feedback_metrics_collect_interval)

        fb = daemon.generate_feedback()
        -- Check is metrics were reset. Fiber yields on feedback generation, so
        -- metrics may be not empty.
        t.assert(fb.metrics == nil or #fb.metrics < 10)
    end)
end

g.test_no_metrics = function()
    g.server:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local has_metrics, _ = pcall(require, 'metrics')
        t.skip_if(has_metrics)
        fiber.sleep(0.3)
        local daemon = box.internal.feedback_daemon
        local fb = daemon.generate_feedback()
        t.assert_equals(fb.metrics, nil)
    end)
end

g.test_metrics_no_collect = function()
    g.server:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local m = {}
        m._VERSION = "0.16.0"
        package.loaded["metrics"] = m
        local daemon = box.internal.feedback_daemon
        -- Dump leftover metrics from previous tests, if any.
        daemon.generate_feedback()
        fiber.sleep(0.1)
        local fb = daemon.generate_feedback()
        t.assert_equals(fb.metrics, nil)
    end)
end

g.test_disable_metrics = function()
    g.server:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        box.cfg{feedback_send_metrics = false}
        local m = {}
        m.collect = function(_)
            return {1, 2}
        end
        m._VERSION = "0.16.0"
        package.loaded["metrics"] = m
        fiber.sleep(0.1)
        local daemon = box.internal.feedback_daemon
        local fb = daemon.generate_feedback()
        t.assert_equals(fb.metrics, nil)
    end)
end

g.test_metrics_old_version = function()
    g.server:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        local m = {}
        m.collect = function(_)
            return {1, 2}
        end
        -- We identify new version by defined _VERSION field.
        -- It is not defined here - module metrics is not relevant then.
        package.loaded["metrics"] = m
        fiber.sleep(0.1)
        local daemon = box.internal.feedback_daemon
        local fb = daemon.generate_feedback()
        t.assert_equals(fb.metrics, nil)
    end)
end

g.test_memory_limit = function()
    g.server:exec(function()
        local t = require('luatest')
        local fiber = require('fiber')
        box.cfg{feedback_metrics_limit = 20 * 5}
        local m = {}
        m.collect = function(_)
            return "abc"
        end
        m._VERSION = "0.16.0"
        package.loaded["metrics"] = m
        fiber.sleep(0.5)
        local daemon = box.internal.feedback_daemon
        local fb = daemon.generate_feedback()
        t.assert_type(fb.metrics, 'table')
        t.assert_equals(#fb.metrics, 5)
        box.cfg{feedback_metrics_limit = 1}
        fiber.sleep(0.3)
        fb = daemon.generate_feedback()
        t.assert_equals(fb.metrics, nil)
    end)
end
