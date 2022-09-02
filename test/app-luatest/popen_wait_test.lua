local fiber = require('fiber')
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
    t.assert_is(err.type, 'HandleIsClosed')
    t.assert_is(err.message, 'popen: a handle was closed during an operation')
end

-- The :wait() call must overcome spurious wakeups.
--
-- The test case checks it in the following way:
--
-- * Run `sleep 120` in a child process.
-- * Run a fiber and block it on :wait().
-- * Wake up the fiber several times (during ~1 second)
-- * Verify that the :wait() call didn't returned (during ~1 more
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
