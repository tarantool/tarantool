local compat = require('tarantool').compat
local t = require('luatest')
local g = t.group()
local fiber = require('fiber')

g.test_channel_close_default = function()
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

g.test_channel_close_old = function()
    compat.fiber_channel_close_mode = 'old'
    local ch = fiber.channel(10)
    ch:put('one')
    ch:put('two')
    t.assert_equals(ch:get(), 'one')
    ch:close()
    t.assert(ch:is_closed())
    t.assert_not(ch:put('three'))
    t.assert_equals(ch:get(), nil)
end

g.test_channel_close_new = function()
    -- Graceful close (new) selected.
    compat.fiber_channel_close_mode = 'new'
    local ch = fiber.channel(10)
    ch:put('one')
    ch:put('two')
    ch:close()

    t.assert(ch:is_closed())
    t.assert_not(ch:put('three'))

    t.assert_equals(ch:get(), 'one')
    t.assert(ch:is_closed())

    t.assert_equals(ch:get(), 'two')
    t.assert(ch:is_closed())

    t.assert_equals(ch:get(), nil)
end

g.test_close_graceful_on_empty = function()
    compat.fiber_channel_close_mode = 'new'
    local ch = fiber.channel(10)
    ch:put('one')
    ch:get()
    ch:close()
    t.assert(ch:is_closed())
end

local test_close_reader_payload = function(mode)
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

g.test_close_reader_old = function()
    test_close_reader_payload('old')
end

g.test_close_reader_new = function()
    test_close_reader_payload('new')
end

local test_close_writer_payload = function(mode)
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

g.test_close_writer_old = function()
    test_close_writer_payload('old')
end

g.test_close_writer_new = function()
    test_close_writer_payload('new')
end
