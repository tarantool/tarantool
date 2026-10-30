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
-- Ensure the request is garbage collected both if is not used and
-- if is.
--
gc_test = setmetatable({}, {__mode = 'v'})
gc_test.future = c:call('long_function', {1, 2, 3}, {is_async = true})
gc_test.future ~= nil
collectgarbage()
gc_test
finalize_long()

future = c:call('long_function', {1, 2, 3}, {is_async = true})
collectgarbage()
future ~= nil
finalize_long()
future:wait_result(1000)
collectgarbage()
future ~= nil
gc_test.future = future
future = nil
collectgarbage()
gc_test

c:close()
s:drop()
