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
-- Check long connections, multiple wait_result().
--
future = c:call('long_function', {1, 2, 3}, {is_async = true})
future:result()
future:is_ready()
future:wait_result(0.01) -- Must fail on timeout.
finalize_long()
ret = future:wait_result(100)
future:is_ready()
-- Any timeout is ok - response is received already.
future:wait_result(0)
future:wait_result(0.01)
ret

_, err = pcall(future.wait_result, future, true)
err:find('Usage') ~= nil
_, err = pcall(future.wait_result, future, '100')
err:find('Usage') ~= nil

--
-- Check that there's no unexpected yields.
--
function assert_no_csw(func, ...)               \
    local csw1 = fiber.info()[fiber.id()].csw   \
    local ret = {func(...)}                     \
    local csw2 = fiber.info()[fiber.id()].csw   \
    assert(csw2 - csw1 == 0)                    \
    return unpack(ret)                          \
end
future = c:call('long_function', {1, 2, 3}, {is_async = true})
assert_no_csw(future.is_ready, future)
assert_no_csw(future.result, future)
assert_no_csw(future.wait_result, future, 0)
finalize_long()
future:wait_result()
assert_no_csw(future.is_ready, future)
assert_no_csw(future.result, future)
assert_no_csw(future.wait_result, future)

box.schema.func.drop('long_function')

c:close()
s:drop()
