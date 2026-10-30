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
-- Test space methods.
--
c:close()
box.schema.user.grant('guest', 'read,write', 'space', 'test')
c = net:connect(box.cfg.listen)
future = c.space.test:select({1}, {is_async = true})
ret = future:wait_result(100)
ret
type(ret[1])
future = c.space.test:insert({5}, {is_async = true})
future:wait_result(100)
s:get{5}
future = c.space.test:replace({6}, {is_async = true})
future:wait_result(100)
s:get{6}
future = c.space.test:delete({6}, {is_async = true})
future:wait_result(100)
s:get{6}
future = c.space.test:update({5}, {{'=', 2, 5}}, {is_async = true})
future:wait_result(100)
s:get{5}
future = c.space.test:upsert({5}, {{'=', 2, 6}}, {is_async = true})
future:wait_result(100)
s:get{5}
future = c.space.test:get({5}, {is_async = true})
future:wait_result(100)

--
-- Test index methods.
--
future = c.space.test.index.pk:select({1}, {is_async = true})
future:wait_result(100)
future = c.space.test.index.pk:get({2}, {is_async = true})
future:wait_result(100)
future = c.space.test.index.pk:min({}, {is_async = true})
future:wait_result(100)
future = c.space.test.index.pk:max({}, {is_async = true})
future:wait_result(100)
c:close()
box.schema.user.grant('guest', 'execute', 'universe')
c = net:connect(box.cfg.listen)
future = c.space.test.index.pk:count({3}, {is_async = true})
future:wait_result(100)
c:close()
box.schema.user.revoke('guest', 'execute', 'universe')
c = net:connect(box.cfg.listen)
future = c.space.test.index.pk:delete({3}, {is_async = true})
future:wait_result(100)
s:get{3}
future = c.space.test.index.pk:update({4}, {{'=', 2, 6}}, {is_async = true})
future:wait_result(100)
s:get{4}

--
-- Test async errors.
--
future = c.space.test:insert({1}, {is_async = true})
future:wait_result()
future:result()

box.schema.func.drop('long_function')

c:close()
s:drop()
