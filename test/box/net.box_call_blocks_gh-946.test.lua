fiber = require 'fiber'
test_run = require('test_run').new()

net = require('net.box')

test_run:cmd('create server connecter with script = "box/proxy.lua"')

--
-- gh-946: long polling CALL blocks input
--
box.schema.func.create('fast_call')
box.schema.func.create('long_call')
box.schema.func.create('wait_signal')
box.schema.user.grant('guest', 'execute', 'function', 'fast_call')
box.schema.user.grant('guest', 'execute', 'function', 'long_call')
box.schema.user.grant('guest', 'execute', 'function', 'wait_signal')
c = net.connect(box.cfg.listen)

N = 100

pad = string.rep('x', 1024)

long_call_cond = fiber.cond()
long_call_channel = fiber.channel()
fast_call_channel = fiber.channel()

function fast_call(x) return x end
function long_call(x) long_call_cond:wait() return x * 2 end

test_run:cmd("setopt delimiter ';'")
for i = 1, N do
    fiber.create(function()
        fast_call_channel:put(c:call('fast_call', {i, pad}))
    end)
    fiber.create(function()
        long_call_channel:put(c:call('long_call', {i, pad}))
    end)
end
test_run:cmd("setopt delimiter ''");

x = 0
for i = 1, N do x = x + fast_call_channel:get() end
x

long_call_cond:broadcast()

x = 0
for i = 1, N do x = x + long_call_channel:get() end
x

--
-- Check that a connection does not leak if there is
-- a long CALL in progress when it is closed.
--
disconnected = false
function on_disconnect() disconnected = true end

-- Make sure all dangling connections are collected so
-- that on_disconnect trigger isn't called spuriously.
collectgarbage('collect')
fiber.sleep(0)

box.session.on_disconnect(on_disconnect) == on_disconnect
