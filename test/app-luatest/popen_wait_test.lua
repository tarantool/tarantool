local fiber = require('fiber')
local ffi = require('ffi')
local popen = require('popen')

local t = require('luatest')
local g = t.group()

local function sleep_120()
    return popen.new({'sleep 120'}, {
        stdin = popen.opts.CLOSE,
        stdout = popen.opts.CLOSE,
        stderr = popen.opts.CLOSE,
        shell = true,
        setsid = true,
        group_signal = true,
    })
end

local function assert_popen_wait_raises_illegal_params(f)
    local ok, err = pcall(f)
    t.assert_equals({
        ok = ok,
        error_type = err.type,
        error_message = err.message,
    }, {
        ok = false,
        error_type = 'IllegalParams',
        error_message = 'Bad params, use: ph:wait([{timeout = <number>}])',
    })
end

-- The :wait() call must return if the handle is closed.
g.test_wait_unblock_after_close = function()
    local ph = sleep_120()

    -- Block the fiber 'f' at :wait() call.
    local res, err
    local latch = fiber.channel(1)
    fiber.create(function()
        res, err = ph:wait()
        latch:put(true)
    end)

    -- Close the handle.
    ph:close()

    -- Wait 1 second at max.
    local wait_is_completed = latch:get(1)
    t.assert(wait_is_completed)

    -- Verify the reported error.
    t.assert_is(res, nil)
    t.assert_type(err, 'cdata')
    t.assert_is(err.type, 'ChannelIsClosed')
end

-- The :wait() call must overcome spurious wakeups.
--
-- The test case checks it in the following way:
--
-- * Run `sleep 120` in a child process.
-- * Run a fiber and block it on :wait().
-- * Wake up the fiber several times (during ~1 second)
-- * Verify that the :wait() call doesn't return (during ~1 more
--   second).
g.test_wait_spurious_wakeup = function()
    local ph = sleep_120()

    -- Block the fiber 'f' at :wait() call.
    local latch = fiber.channel(1)
    local f = fiber.create(function()
        ph:wait()
        latch:put(true)
    end)

    -- Spurious wakeups.
    for _ = 1, 10 do
        f:wakeup()
        fiber.sleep(0.1)
    end

    -- Wait 1 second at max.
    local wait_is_completed = latch:get(1)
    t.assert_not(wait_is_completed)

    ph:close()
end

-- The :wait() call must unblock on fiber cancel.
g.test_wait_cancelled = function()
    local ph = sleep_120()

    -- Block the fiber 'f' at :wait() call.
    local ok, err
    local latch = fiber.channel(1)
    local f = fiber.create(function()
        ok, err = pcall(ph.wait, ph)
        latch:put(true)
    end)

    -- Cancel the :wait() call.
    f:cancel()

    -- Wait 1 second at max.
    local wait_is_completed = latch:get(1)
    t.assert(wait_is_completed)

    -- Verify that the FiberIsCancelled error is raised.
    t.assert_is(ok, false)
    t.assert_type(err, 'cdata')
    t.assert_is(err.type, 'FiberIsCancelled')

    ph:close()
end

-- The :wait() call must return when a timeout is reached.
g.test_wait_timeout = function()
    local ph = sleep_120()

    -- Block a fiber at :wait() call.
    local res, err
    local latch = fiber.channel(1)
    fiber.create(function()
        res, err = ph:wait({timeout = 0.1})
        latch:put(true)
    end)

    -- Wait 1 second at max.
    local wait_is_completed = latch:get(1)
    t.assert(wait_is_completed)

    -- Verify that the TimedOut error is returned.
    t.assert_is(res, nil)
    t.assert_type(err, 'cdata')
    t.assert_is(err.type, 'TimedOut')

    ph:close()
end

-- Verify how :wait()'s argument (opts) is checked.
g.test_wait_opts_typecheck = function()
    local ph = sleep_120()

    assert_popen_wait_raises_illegal_params(function()
        return ph:wait('not a table')
    end)

    assert_popen_wait_raises_illegal_params(function()
        return ph:wait(1)
    end)

    -- Verify that :wait(nil) is accepted.
    --
    -- Block the fiber 'f' at :wait() call and wait 1 second at
    -- max.
    local latch = fiber.channel(1)
    fiber.create(function()
        ph:wait(nil)
        latch:put(true)
    end)
    local wait_is_completed = latch:get(1)
    t.assert_not(wait_is_completed)

    -- Verify that :wait({}) is accepted.
    --
    -- Block the fiber 'f' at :wait() call and wait 1 second at
    -- max.
    local latch = fiber.channel(1)
    fiber.create(function()
        ph:wait({})
        latch:put(true)
    end)
    local wait_is_completed = latch:get(1)
    t.assert_not(wait_is_completed)

    ph:close()
end

-- Verify how :wait()'s option timeout is checked.
g.test_wait_timeout_typecheck = function()
    local ph = sleep_120()

    -- String, cdata, table are invalid timeout values.
    assert_popen_wait_raises_illegal_params(function()
        return ph:wait({timeout = '1'})
    end)
    assert_popen_wait_raises_illegal_params(function()
        return ph:wait({timeout = ffi.new('float', 1.1)})
    end)
    assert_popen_wait_raises_illegal_params(function()
        return ph:wait({timeout = {}})
    end)

    -- gh-9976: accept a negative timeout and interpret it as
    -- zero.
    assert_popen_wait_raises_illegal_params(function()
        return ph:wait({timeout = -2})
    end)

    -- gh-4911: support number64 for timeout and signal.
    assert_popen_wait_raises_illegal_params(function()
        return ph:wait({timeout = 1LL})
    end)
    assert_popen_wait_raises_illegal_params(function()
        return ph:wait({timeout = 1ULL})
    end)

    ph:close()
end
