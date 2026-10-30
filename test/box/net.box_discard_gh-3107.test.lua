fiber = require 'fiber'
net = require('net.box')

--
-- gh-3107: fiber-async netbox.
--
cond = nil
box.schema.func.create('long_function')
box.schema.user.grant('guest', 'execute', 'function', 'long_function')
function long_function(...) cond = fiber.cond() cond:wait() return ... end
function finalize_long() while not cond do fiber.sleep(0.01) end cond:signal() cond = nil end
s = box.schema.create_space('test')
pk = s:create_index('pk')
s:replace{1}
s:replace{2}
s:replace{3}
s:replace{4}
c = net:connect(box.cfg.listen)

--
-- Ensure a request can be finalized from non-caller fibers.
--
future = c:call('long_function', {1, 2, 3}, {is_async = true})
ret = {}
count = 0
for i = 1, 10 do fiber.create(function() ret[i] = future:wait_result(1000) count = count + 1 end) end
future:wait_result(0.01) -- Must fail on timeout.
finalize_long()
while count ~= 10 do fiber.sleep(0.1) end
ret

--
-- Test discard.
--
future = c:call('long_function', {1, 2, 3}, {is_async = true})
ch = fiber.channel()
_ = fiber.create(function() ch:put({future:wait_result()}) end)
future:discard()
ch:get(100)
finalize_long()
future:result()
future:wait_result(100)

box.schema.func.drop('long_function')

c:close()
s:drop()
