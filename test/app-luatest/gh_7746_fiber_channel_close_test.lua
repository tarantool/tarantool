local server = require('luatest.server')
local t = require('luatest')

local function server_test_channel_close_default()
    local fiber = require('fiber')
    local t = require('luatest')

    -- No graceful close (old) by default.
    local ch = fiber.channel(10)
    ch:put('one')
    ch:put('two')
    t.assert_equals(ch:get(), 'one')
    ch:close()
    t.assert(ch:is_closed())
    t.assert_not(ch:put('three'))
    t.assert_equals(ch:get(), nil)
end

local function server_test_channel_close(mode)
    local compat = require('tarantool').compat
    local fiber = require('fiber')
    local t = require('luatest')

    -- Graceful close (new) selected.
    compat.fiber_channel_close_mode = mode
    local ch = fiber.channel(10)
    ch:put('one')
    ch:put('two')
    ch:put('three')

    t.assert_equals(ch:get(), 'one')
    ch:close()

    t.assert(ch:is_closed())
    t.assert_not(ch:put('four'))

    if mode == 'new' then
        t.assert_equals(ch:get(), 'two')
        t.assert(ch:is_closed())

        t.assert_equals(ch:get(), 'three')
        t.assert(ch:is_closed())
    end

    t.assert_equals(ch:get(), nil)
end

local server_test_close_reader_payload = function(mode)
    local compat = require('tarantool').compat
    local fiber = require('fiber')
    local t = require('luatest')

    compat.fiber_channel_close_mode = mode
    local ch = fiber.channel(0)
    local reader = fiber.new(function()
        t.assert_not(ch:is_closed(),
                     'reader tries to read from the open channel')
        -- Try to obtain the message from the zero-length
        -- channel.
        -- XXX: <reader> fiber hangs forever, until one of the
        -- following occurs:
        -- * <fiber_channel.put> is called from another fiber
        -- * <channel> is closed from another fiber
        -- For the latter case <fiber_channel.get> fails (i.e.
        -- yields nil value).
        t.assert_is(ch:get(), nil,
                    'reader fails to read from the zero-length channel')
        t.assert(ch:is_closed(), 'reader hangs until channel is closed')
        t.assert_is(ch:get(0), nil,
                    'reader fails to read from the closed channel')
    end)
    reader:set_joinable(true)
    -- Yield to start tests in <reader> payload.
    fiber.yield()
    ch:close()
    t.assert(ch:is_closed())
    -- Wait until tests in <writer> payload are finished.
    local status, err = fiber.join(reader)
    t.assert(status, err)
end

local server_test_close_writer_payload = function(mode)
    local compat = require('tarantool').compat
    local fiber = require('fiber')
    local t = require('luatest')

    compat.fiber_channel_close_mode = mode
    local ch = fiber.channel(0)
    local writer = fiber.new(function()
        t.assert_not(ch:is_closed(),
                     'writer tries to write to the open channel')
        -- Try to push the message into the zero-length
        -- channel.
        -- XXX: <writer> fiber hangs forever, until one of the
        -- following occurs:
        -- * <fiber_channel.get> is called from another fiber
        -- * <channel> is closed from another fiber
        -- For the latter case <fiber_channel.put> fails (i.e.
        -- yields false value).
        t.assert_not(ch:put('one'),
                     'writer fails to write to the zero-length channel')
        t.assert(ch:is_closed(), 'writer hangs until channel is closed')
        t.assert_not(ch:put('one'),
                     'writer fails to write to the closed channel')
    end)
    writer:set_joinable(true)
    -- Yield to start tests in <writer> payload.
    fiber.yield()
    ch:close()
    t.assert(ch:is_closed())
    -- Wait until tests in <writer> payload are finished.
    local status, err = fiber.join(writer)
    t.assert(status, err)
end

local tests = {
    server_test_channel_close,
    server_test_channel_close_default,
    server_test_close_writer_payload,
    server_test_close_reader_payload,
}

local g = t.group('pgroup', t.helpers.matrix{test_func = tests,
                                             mode = {'new', 'old'}})

g.test_fiber_close = function(cg)
    local s = server:new{alias = 'default'}
    s:start()
    s:exec(cg.params.test_func, {cg.params.mode})
    s:stop()
end
