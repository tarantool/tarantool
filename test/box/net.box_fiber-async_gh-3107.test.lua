fiber = require 'fiber'
net = require('net.box')

--
-- gh-3107: fiber-async netbox.
--
cond = nil
box.schema.user.grant('guest', 'execute', 'universe')
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

-- Storing user data in future object.
future = c:eval('return 123', {}, {is_async = true})
future.abc -- nil
future.abc = nil    -- delete a key that has never been set
future.abc -- nil
future.abc = 'abc'  -- set a value for a key
future.abc -- abc
future.abc = nil    -- delete a value for a key
future.abc -- nil
future.abc = nil    -- delete a key with no value
future.abc -- nil
future:wait_result() -- 123

-- Garbage collection of stored user data.
future = c:eval('return 123', {}, {is_async = true})
future.data1 = {1}
future.data2 = {2}
future.data3 = {3}
gc = setmetatable({}, {__mode = 'v'})
gc.data1 = future.data1
gc.data2 = future.data2
gc.data3 = future.data3
future.data1 = nil
_ = collectgarbage('collect')
gc.data1 == nil
future.data2 = 123
_ = collectgarbage('collect')
gc.data2 == nil
future:wait_result() -- 123
future = nil
_ = collectgarbage('collect')
_ = collectgarbage('collect')
gc.data3 == nil

--
-- __tostring and __serialize future methods
--
future = c:eval('return 123', {}, {is_async = true})
tostring(future)
future
future.abc = 123
future.xyz = 'abc'
tostring(future)
future
future:wait_result()

box.schema.user.revoke('guest', 'execute', 'universe')

c:close()
s:drop()
