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
box.cfg.reload()
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
    box.fiber.detach('started')
    space = box.space['tweedledum']
    while true do
        space:replace{1953719668, os.time()}
        box.fiber.sleep(0.001)
    end
end;
f = box.fiber.create(y);
box.fiber.resume(f);
box.fiber.sleep(0.002);
box.fiber.cancel(f);
box.fiber.resume(f);
for k = 1, 1000, 1 do
    box.fiber.create(
        function()
            box.fiber.detach()
        end
    )
end;
--# setopt delimiter ''

collectgarbage('collect')
-- check that these newly created fibers are garbage collected
box.fiber.find(900)
box.fiber.find(910)
box.fiber.find(920)
box.fiber.find()
box.fiber.find('test')
--  https://github.com/tarantool/tarantool/issues/131
--  box.fiber.resume(box.fiber.cancel()) -- hang
f = box.fiber.create(function() box.fiber.cancel(box.fiber.self()) end)
box.fiber.resume(f)
f = nil
-- https://github.com/tarantool/tarantool/issues/119
ftest = function() box.fiber.sleep(0.01 * math.random() ) return true end

--# setopt delimiter ';'
for i = 1, 10 do
    result = {}
    for j = 1, 300 do
        box.fiber.resume(box.fiber.create(function()
            box.fiber.detach()
            table.insert(result, ftest())
        end))
    end
    while #result < 300 do box.fiber.sleep(0.01) end
end;
--# setopt delimiter ''
#result

--# setopt delimiter ''
--
-- 
--  Test box.fiber.wrap()
-- 
--  This should try to infinitely create fibers,
--  but hit the fiber stack size limit and fail
--  with an error.
f = function() box.fiber.wrap(f) end
f()
-- 
-- Test argument passing
-- 
f = function(a, b) box.fiber.wrap(function(arg) result = arg end, a..b) end
f('hello ', 'world')
result
f('bye ', 'world')
result
-- 
-- Test that the created fiber is detached
-- 
box.fiber.wrap(function() result = box.fiber.status() end)
result
-- A test case for Bug#933487
-- tarantool crashed during shutdown if non running LUA fiber
-- was created
f = box.fiber.create(function () return true end)
box.snapshot()
box.snapshot()
box.snapshot()
box.fiber.resume(f)
f = box.fiber.create(function () return true end)

box.fiber.sleep(0)
box.fiber.sleep(0.01)
box.fiber.sleep(0.0001)
box.fiber.sleep('hello')
box.fiber.sleep(box, 0.001)
box.fiber.cancel(box.fiber.self())
f = box.fiber.self()
old_id = f:id()
box.fiber.cancel(f)
box.fiber.self():id() - old_id < 3
box.fiber.cancel(box.fiber.self())
box.fiber.self():id() - old_id < 5
g = box.fiber.self()
f==g
function r() f = box.fiber.create(r) return (box.fiber.resume(f)) end
r()
f = box.fiber.create(print('hello')
box.fiber.resume(f)
-- test passing arguments in and out created fiber
function r(a, b) return a, b end
f=box.fiber.create(r)
box.fiber.resume(f)
f=box.fiber.create(r)
box.fiber.resume(f, 'hello')
f=box.fiber.create(r)
box.fiber.resume(f, 'hello', 'world')
f=box.fiber.create(r)
box.fiber.resume(f, 'hello', 'world', 'wide')
function y(a, b) c=box.fiber.yield(a) return box.fiber.yield(b, c) end
f=box.fiber.create(y)
box.fiber.resume(f, 'hello', 'world')
box.fiber.resume(f, 'wide')
box.fiber.resume(f)
function y() box.fiber.detach() while true do box.replace(0, 1953719668, os.time()) box.fiber.sleep(0.001) end end
f = box.fiber.create(y)
box.fiber.resume(f)
box.fiber.sleep(0.002)
box.fiber.cancel(f)
box.fiber.resume(f)
f=nil
for k=1, 10000, 1 do box.fiber.create(function() box.fiber.detach() end) end
collectgarbage('collect')
-- check that these newly created fibers are garbage collected
box.fiber.find(9000)
box.fiber.find(9010)
box.fiber.find(9020)

--  test box.fiber.status functions: invalid arguments
box.fiber.status(1)
box.fiber.status('fafa-gaga')
box.fiber.status(nil)

--  A test case for Bug#911641 box.fiber.sleep() works incorrectly if
--  a fiber is attached.
function r() return box.fiber.sleep(0.01) end
f = box.fiber.create(r)
box.fiber.resume(f)
box.fiber.resume(f)
function r() box.fiber.yield(box.space.tweedledum:insert{0, 0, 1}) box.fiber.yield(box.space.tweedledum:select{0}) box.fiber.yield(box.space.tweedledum:truncate()) end
f = box.fiber.create(r)
box.fiber.resume(f)
box.fiber.resume(f)
box.fiber.resume(f)
box.fiber.resume(f)
function r() return box.fiber.yield(box.fiber.create(r)) end
f = r()
f1 = box.fiber.resume(f)
f2 = box.fiber.resume(f1)
f3 = box.fiber.resume(f2)
f4 = box.fiber.resume(f3)
f5 = box.fiber.resume(f4)
f6 = box.fiber.resume(f5)
f7 = box.fiber.resume(f6)
f8 = box.fiber.resume(f7)
f9 = box.fiber.resume(f8)
f10 = box.fiber.resume(f9)
f11 = box.fiber.resume(f10)
f12 = box.fiber.resume(f11)
f13 = box.fiber.resume(f12)
f14 = box.fiber.resume(f13)
f15 = box.fiber.resume(f14)
f16 = box.fiber.resume(f15)
f17 = box.fiber.resume(f16)
box.fiber.resume(f)
box.fiber.resume(f1)
box.fiber.resume(f2)
box.fiber.resume(f3)
box.fiber.resume(f4)
box.fiber.resume(f5)
box.fiber.resume(f6)
box.fiber.resume(f7)
box.fiber.resume(f8)
box.fiber.resume(f9)
box.fiber.resume(f10)
box.fiber.resume(f11)
box.fiber.resume(f12)
box.fiber.resume(f13)
box.fiber.resume(f14)
box.fiber.resume(f15)
box.fiber.resume(f16)
f17 = nil
function r() box.fiber.detach() box.fiber.sleep(1000) end
f = box.fiber.create(r)
box.fiber.resume(f)
box.fiber.resume(f)
box.fiber.cancel(f)
box.fiber.resume(f)
--  Test box.fiber.name()
old_name = box.fiber.name()
box.fiber.name() == old_name
box.fiber.self():name() == old_name
box.fiber.name('hello fiber')
box.fiber.name()
box.fiber.self():name('bye fiber')
box.fiber.self():name()
box.fiber.self():name(old_name)

space:drop()

-- box.fiber test (create, resume, yield, status)
dofile("fiber.lua")
-- print run fiber's test
box_fiber_run_test()
