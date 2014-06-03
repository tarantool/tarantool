fiber = require('fiber')
space = box.schema.create_space('tweedledum')
space:create_index('primary', { type = 'hash' })
-- A test case for a race condition between ev_schedule
-- and wal_schedule fiber schedulers.
-- The same fiber should not be scheduled by ev_schedule (e.g.
-- due to cancellation) if it is within th wal_schedule queue.
-- The test case is dependent on rows_per_wal, since this is when
-- we reopen the .xlog file and thus wal_scheduler takes a long
-- pause
box.cfg.rows_per_wal
space:insert{1, 'testing', 'lua rocks'}
space:delete{1}
space:insert{1, 'testing', 'lua rocks'}
space:delete{1}

space:insert{1, 'test box delete'}
space:delete{1}
space:insert{1, 'test box delete'}
space:delete{1}
space:insert{1684234849, 'test box delete'}
space:delete{1684234849}
space:insert{1684234849, 'test box delete'}
space:delete{1684234849}
space:insert{1684234849, 'test box.select()'}
space:replace{1684234849, 'hello', 'world'}
space:replace{1667655012, 'goodbye', 'universe'}
space:replace{1667655012, 'goodbye', 'universe'}
space:replace{1667655012, 'goodbye', 'universe'}
space:replace{1667655012, 'goodbye', 'universe'}
space:replace{1667655012, 'goodbye', 'universe'}
space:replace{1667655012, 'goodbye', 'universe'}
space:replace{1667655012, 'goodbye', 'universe'}
space:replace{1667655012, 'goodbye', 'universe'}
space:replace{1667655012, 'goodbye', 'universe'}
space:replace{1667655012, 'goodbye', 'universe'}
space:replace{1667655012, 'goodbye', 'universe'}
space:replace{1667655012, 'goodbye', 'universe'}
space:replace{1684234849}
space:delete{1684234849}
space:delete{1667655012}
space:insert{1953719668, 'old', 1684234849}
-- test that insert produces a duplicate key error
space:insert{1953719668, 'old', 1684234849}
space:update(1953719668, {{'=', 0, 1936941424}, {'=', 1, 'new'}})
space:update(1234567890, {{'+', 2, 1}})
space:update(1936941424, {{'+', 2, 1}})
space:update(1936941424, {{'-', 2, 1}})
space:update(1936941424, {{'-', 2, 1}})
space:update(1936941424, {{'+', 2, 1}})
space:delete{1936941424}
-- must be read-only

space:insert{1953719668}
space:insert{1684234849}
space:delete{1953719668}
space:delete{1684234849}
space:insert{1953719668, 'hello world'}
space:update(1953719668, {{'=', 1, 'bye, world'}})
space:delete{1953719668}
-- test tuple iterators
t = space:insert{1953719668}
t = space:replace{1953719668, 'another field'}
t = space:replace{1953719668, 'another field', 'one more'}
space:truncate()
-- test passing arguments in and out created fiber

--# setopt delimiter ';'
function y()
    fiber.detach('started')
    space = box.space['tweedledum']
    while true do
        space:replace{1953719668, os.time()}
        fiber.sleep(0.001)
    end
end;
f = fiber.create(y);
fiber.resume(f);
fiber.sleep(0.002);
fiber.cancel(f);
fiber.resume(f);
for k = 1, 1000, 1 do
    fiber.create(
        function()
            fiber.detach()
        end
    )
end;
--# setopt delimiter ''

collectgarbage('collect')
-- check that these newly created fibers are garbage collected
fiber.find(900)
fiber.find(910)
fiber.find(920)
fiber.find()
fiber.find('test')
--  https://github.com/tarantool/tarantool/issues/131
--  fiber.resume(fiber.cancel()) -- hang
f = fiber.create(function() fiber.cancel(fiber.self()) end)
fiber.resume(f)
f = nil
-- https://github.com/tarantool/tarantool/issues/119
ftest = function() fiber.sleep(0.01 * math.random() ) return true end

--# setopt delimiter ';'
for i = 1, 10 do
    result = {}
    for j = 1, 300 do
        fiber.resume(fiber.create(function()
            fiber.detach()
            table.insert(result, ftest())
        end))
    end
    while #result < 300 do fiber.sleep(0.01) end
end;
--# setopt delimiter ''
#result

--# setopt delimiter ''
--
-- 
--  Test fiber.wrap()
-- 
--  This should try to infinitely create fibers,
--  but hit the fiber stack size limit and fail
--  with an error.
f = function() fiber.wrap(f) end
f()
-- 
-- Test argument passing
-- 
f = function(a, b) fiber.wrap(function(arg) result = arg end, a..b) end
f('hello ', 'world')
result
f('bye ', 'world')
result
-- 
-- Test that the created fiber is detached
-- 
fiber.wrap(function() result = fiber.status() end)
result
-- A test case for Bug#933487
-- tarantool crashed during shutdown if non running LUA fiber
-- was created
f = fiber.create(function () return true end)
box.snapshot()
box.snapshot()
box.snapshot()
fiber.resume(f)
f = fiber.create(function () return true end)

fiber.sleep(0)
fiber.sleep(0.01)
fiber.sleep(0.0001)
fiber.sleep('hello')
fiber.sleep(box, 0.001)
fiber.cancel(fiber.self())
f = fiber.self()
old_id = f:id()
fiber.cancel(f)
fiber.self():id() - old_id < 3
fiber.cancel(fiber.self())
fiber.self():id() - old_id < 5
g = fiber.self()
f==g
function r() f = fiber.create(r) return (fiber.resume(f)) end
r()
f = fiber.create(print('hello'))
fiber.resume(f)
-- test passing arguments in and out created fiber
function r(a, b) return a, b end
f=fiber.create(r)
fiber.resume(f)
f=fiber.create(r)
fiber.resume(f, 'hello')
f=fiber.create(r)
fiber.resume(f, 'hello', 'world')
f=fiber.create(r)
fiber.resume(f, 'hello', 'world', 'wide')
function y(a, b) c=fiber.yield(a) return fiber.yield(b, c) end
f=fiber.create(y)
fiber.resume(f, 'hello', 'world')
fiber.resume(f, 'wide')
fiber.resume(f)
function y() fiber.detach() while true do box.replace(0, 1953719668, os.time()) fiber.sleep(0.001) end end
f = fiber.create(y)
fiber.resume(f)
fiber.sleep(0.002)
fiber.cancel(f)
fiber.resume(f)
f=nil
for k=1, 10000, 1 do fiber.create(function() fiber.detach() end) end
collectgarbage('collect')
-- check that these newly created fibers are garbage collected
fiber.find(9000)
fiber.find(9010)
fiber.find(9020)

--  test fiber.status functions: invalid arguments
fiber.status(1)
fiber.status('fafa-gaga')
fiber.status(nil)

--  A test case for Bug#911641 fiber.sleep() works incorrectly if
--  a fiber is attached.
function r() return fiber.sleep(0.01) end
f = fiber.create(r)
fiber.resume(f)
fiber.resume(f)
--# setopt delimiter ';'
function r()
    fiber.yield(box.space.tweedledum:insert{0, 0, 1})
    fiber.yield(box.space.tweedledum:get{0})
    fiber.yield(box.space.tweedledum:truncate())
end;
--# setopt delimiter ''
f = fiber.create(r)
fiber.resume(f)
fiber.resume(f)
fiber.resume(f)
fiber.resume(f)
function r() return fiber.yield(fiber.create(r)) end
f = r()
f1 = fiber.resume(f)
f2 = fiber.resume(f1)
f3 = fiber.resume(f2)
f4 = fiber.resume(f3)
f5 = fiber.resume(f4)
f6 = fiber.resume(f5)
f7 = fiber.resume(f6)
f8 = fiber.resume(f7)
f9 = fiber.resume(f8)
f10 = fiber.resume(f9)
f11 = fiber.resume(f10)
f12 = fiber.resume(f11)
f13 = fiber.resume(f12)
f14 = fiber.resume(f13)
f15 = fiber.resume(f14)
f16 = fiber.resume(f15)
f17 = fiber.resume(f16)
fiber.resume(f)
fiber.resume(f1)
fiber.resume(f2)
fiber.resume(f3)
fiber.resume(f4)
fiber.resume(f5)
fiber.resume(f6)
fiber.resume(f7)
fiber.resume(f8)
fiber.resume(f9)
fiber.resume(f10)
fiber.resume(f11)
fiber.resume(f12)
fiber.resume(f13)
fiber.resume(f14)
fiber.resume(f15)
fiber.resume(f16)
f17 = nil
function r() fiber.detach() fiber.sleep(1000) end
f = fiber.create(r)
fiber.resume(f)
fiber.resume(f)
fiber.cancel(f)
fiber.resume(f)
--  Test fiber.name()
old_name = fiber.name()
fiber.name() == old_name
fiber.self():name() == old_name
fiber.name('hello fiber')
fiber.name()
fiber.self():name('bye fiber')
fiber.self():name()
fiber.self():name(old_name)

space:drop()

-- box.fiber test (create, resume, yield, status)
dofile("fiber.lua")
-- print run fiber's test
box_fiber_run_test()

function testfun() while true do fiber.sleep(10) end end
f = fiber.wrap(testfun)
f:cancel()
f:resume()
fib_id = fiber.wrap(testfun):id()
fiber.find(fib_id):cancel()
fiber.find(fib_id)
box.fiber = nil
