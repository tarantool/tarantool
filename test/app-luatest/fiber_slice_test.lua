local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_test('test_warning_slice', function()
    local log = require('log')
    g.server = server:new{
        alias   = 'default',
        box_cfg = {
            log_level = log.WARN,
        },
    }
    g.server:start()
end)

g.after_test('test_warning_slice', function()
    g.server:drop()
end)

g.test_fiber_slice = function()
    local fiber = require('fiber')
    local clock = require('clock')

    local check_fiber_slice = function(fiber_f, expected_timeout, delta, set_custom_timeout)
        local fib = fiber.new(fiber_f)
        fib:set_joinable(true)
        if set_custom_timeout then
            fib:set_max_slice(expected_timeout)
        end
        local overhead = clock.monotonic()
        overhead = clock.monotonic() - overhead
        local start_time = clock.monotonic()
        local _, err = fib:join()
        t.assert_equals(tostring(err), "fiber slice is exceeded")
        local time_elapsed = clock.monotonic() - start_time - overhead
        t.assert_almost_equals(time_elapsed, expected_timeout, delta)
    end

    local timeout = 0.4
    -- delta is quite large to pass this test in debug mode
    local delta = 0.25
    fiber.set_max_slice(timeout)

    local function check_slice()
        while true do
            fiber.check_slice()
        end
    end
    -- Test fiber.check_slice() method and deadline mechanism itself.
    check_fiber_slice(check_slice, timeout, delta)

    local function check_extend()
        fiber.extend_slice(timeout / 2)
        while true do
            fiber.check_slice()
        end
    end
    -- Check fiber.extend_slice().
    check_fiber_slice(check_extend, timeout * 1.5, delta)

    local function check_extend_with_table()
        fiber.extend_slice({warn = timeout / 2, err = timeout / 2})
        while true do
            fiber.check_slice()
        end
    end
    -- Check fiber.extend_slice() with table argument.
    check_fiber_slice(check_extend_with_table, timeout * 1.5, delta)

    -- Check fiber with custom slice.
    check_fiber_slice(check_slice, timeout / 2, delta, true)

    local function set_slice_from_inside()
        fiber.set_slice({warn = timeout / 2, err = timeout / 2})
        while true do
            fiber.check_slice()
        end
    end
    -- Set custom slice from inside the fiber.
    -- Also check fiber.set_slice() with table argument.
    check_fiber_slice(set_slice_from_inside, timeout / 2, delta)

    -- Check that default deadline timeout have not been changed after all.
    check_fiber_slice(check_slice, timeout, delta)

    -- Check set_default_deadline method with table argument.
    fiber.set_max_slice({warn = timeout * 1.5, err = timeout * 1.5})
    check_fiber_slice(check_slice, timeout * 1.5, delta)

    local function default_inside_fiber()
        fiber.set_max_slice(timeout / 2)
        while true do
            fiber.check_slice()
        end
    end
    -- Check that setting default slice will change current slice if it is not custom.
    check_fiber_slice(default_inside_fiber, timeout / 2, delta)

    local function custom_against_default()
        fiber.self():set_max_slice(timeout / 2)
        fiber.set_max_slice(timeout * 2)
        while true do
            fiber.check_slice()
        end
    end
    -- Check that setting default slice will not change current slice if it is custom.
    check_fiber_slice(custom_against_default, timeout / 2, delta)
end

g.test_invalid_slice = function()
    local fiber = require('fiber')

    local MIN_TIME = 1000
    local invalid_slices = {}
    table.insert(invalid_slices, {err = -1 * math.random(MIN_TIME), warn = 0})
    table.insert(invalid_slices, {err = 0, warn = -1 * math.random(MIN_TIME)})
    table.insert(invalid_slices, {err = -1 * math.random(MIN_TIME), warn = -1 * math.random(MIN_TIME)})
    table.insert(invalid_slices, -1 * math.random(MIN_TIME))
    local err_msg = "slice must be greater than 0"
    for _, slice in pairs(invalid_slices) do
        t.assert_error_msg_equals(err_msg, fiber.set_slice, slice)
        t.assert_error_msg_equals(err_msg, fiber.extend_slice, slice)
        t.assert_error_msg_equals(err_msg, fiber.set_max_slice, slice)
        t.assert_error_msg_equals(err_msg, fiber.set_max_slice, fiber.self(), slice)
    end
end

g.test_warning_slice = function()
    g.server:exec(function()
        local fiber = require('fiber')
        fiber.set_slice({warn = 0.2, err = 0.5})
        pcall(function()
            while true do
                fiber.check_slice()
            end
        end)
    end)
    local msg = g.server:grep_log('fiber has not yielded for more than 0.200 seconds', 4096)
    t.assert(msg ~= nil)
end
