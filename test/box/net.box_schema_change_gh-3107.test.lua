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

box.schema.func.drop('long_function')

--
-- Test async schema version change.
--
function change_schema(i) local tmp = box.schema.create_space('test'..i) return 'ok' end
box.schema.func.create('change_schema')
box.schema.user.grant('guest', 'execute', 'function', 'change_schema')
box.schema.user.grant('guest', 'write', 'space', '_schema')
box.schema.user.grant('guest', 'read,write', 'space', '_space')
box.schema.user.grant('guest', 'create', 'space')
future1 = c:call('change_schema', {'1'}, {is_async = true})
future2 = c:call('change_schema', {'2'}, {is_async = true})
future3 = c:call('change_schema', {'3'}, {is_async = true})
future1:wait_result()
future2:wait_result()
future3:wait_result()

c:close()
s:drop()
box.schema.func.drop('change_schema')
box.schema.user.revoke('guest', 'write', 'space', '_schema')
box.schema.user.revoke('guest', 'read,write', 'space', '_space')
box.schema.user.revoke('guest', 'create', 'space')
