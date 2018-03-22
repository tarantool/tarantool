fiber = require('fiber')
space = box.schema.space.create('tweedledum')
index = space:create_index('primary', { type = 'hash' })
env = require('test_run')
test_run = env.new()

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
space:update(1953719668, {{'=', 1, 1953719668}, {'=', 2, 'new'}})
space:update(1234567890, {{'+', 3, 1}})
space:update(1953719668, {{'+', 3, 1}})
space:update(1953719668, {{'-', 3, 1}})
space:update(1953719668, {{'-', 3, 1}})
space:update(1953719668, {{'+', 3, 1}})
space:delete{1953719668}
-- must be read-only

space:insert{1953719668}
space:insert{1684234849}
space:delete{1953719668}
space:delete{1684234849}
space:insert{1953719668, 'hello world'}
space:update(1953719668, {{'=', 2, 'bye, world'}})
space:delete{1953719668}
-- test tuple iterators
t = space:insert{1953719668}
t = space:replace{1953719668, 'another field'}
t = space:replace{1953719668, 'another field', 'one more'}
space:truncate()
-- test passing arguments in and out created fiber

test_run:cmd("setopt delimiter ';'")
function y()
    space = box.space['tweedledum']
    while true do
        space:replace{1953719668, os.time()}
        fiber.sleep(0.001)
    end
end;
f = fiber.create(y);
fiber.sleep(0.002);
fiber.cancel(f);
-- fiber garbage collection
n = 1000;
ch = fiber.channel(n);
for k = 1, n, 1 do
    fiber.create(
        function()
            fiber.sleep(0)
            ch:put(k)
        end
    )
end;

for k = 1, n, 1 do
    ch:get()
end;
test_run:cmd("setopt delimiter ''");

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
f = nil
-- https://github.com/tarantool/tarantool/issues/119
ftest = function() fiber.sleep(0.0001 * math.random() ) return true end

test_run:cmd("setopt delimiter ';'")
result = 0;
for i = 1, 10 do
    local res = {}
    for j = 1, 300 do
        fiber.create(function() table.insert(res, ftest()) end)
    end
    while #res < 300 do fiber.sleep(0) end
    result = result + #res
end;
test_run:cmd("setopt delimiter ''");
result
--
-- 
--  Test fiber.create()
-- 
--  This should try to infinitely create fibers,
--  but hit the fiber stack size limit and fail
--  with an error.
--
--  2016-03-25 kostja
--
--  fiber call stack depth was removed, we should
--  use runtime memory limit control instead; the
--  old limit was easy to circument with only
--  slightly more complicated fork bomb code
--
-- f = function() fiber.create(f) end
-- f()
-- 
-- Test argument passing
-- 
f = function(a, b) fiber.create(function(arg) result = arg end, a..b) end
f('hello ', 'world')
result
f('bye ', 'world')
result
-- 
-- Test that the created fiber is detached
-- 
local f = fiber.create(function() result = fiber.status() end)
result
-- A test case for Bug#933487
-- tarantool crashed during shutdown if non running LUA fiber
-- was created
f = fiber.create(function () fiber.sleep(1) return true end)
box.snapshot()
_, e = pcall(box.snapshot)
e
_, e = pcall(box.snapshot)
e
f = fiber.create(function () fiber.sleep(1) end)
-- Test fiber.sleep()
fiber.sleep(0)
fiber.sleep(0.01)
fiber.sleep(0.0001)
fiber.sleep('hello')
fiber.sleep(box, 0.001)
-- test fiber.self()
f = fiber.self()
old_id = f:id()
fiber.self():id() - old_id < 3
fiber.self():id() - old_id < 5
g = fiber.self()
f==g
-- arguments to fiber.create
f = fiber.create(print('hello'))
-- test passing arguments in and out created fiber
res = {}
function r(a, b) res = { a, b } end
f=fiber.create(r)
while f:status() == 'running' do fiber.sleep(0) end
res
f=fiber.create(r, 'hello')
while f:status() == 'running' do fiber.sleep(0) end
res
f=fiber.create(r, 'hello, world')
while f:status() == 'running' do fiber.sleep(0) end
res
f=fiber.create(r, 'hello', 'world', 'wide')
while f:status() == 'running' do fiber.sleep(0) end
res
--  test fiber.status functions: invalid arguments
fiber.status(1)
fiber.status('fafa-gaga')
fiber.status(nil)
-- test fiber.cancel
function r() fiber.sleep(1000) end
f = fiber.create(r)
fiber.cancel(f)
while f:status() ~= 'dead' do fiber.sleep(0) end
f:status()
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
-- various...
function testfun() while true do fiber.sleep(10) end end
f = fiber.create(testfun)
f:cancel()
fib_id = fiber.create(testfun):id()
fiber.find(fib_id):cancel()
while fiber.find(fib_id) ~= nil do fiber.sleep(0) end
fiber.find(fib_id)

--
-- Test local storage
--

type(fiber.self().storage)
fiber.self().storage.key = 48
fiber.self().storage.key

test_run:cmd("setopt delimiter ';'")
function testfun(mgmt, ch)
    mgmt:get()
    ch:put(fiber.self().storage.key)
end;
test_run:cmd("setopt delimiter ''");
mgmt = fiber.channel()
ch = fiber.channel()
f = fiber.create(testfun, mgmt, ch)
f.storage.key = 'some value'
mgmt:put("wakeup plz")
ch:get()
ch:close()
mgmt:close()
ch = nil
mgmt = nil
fiber.self().storage.key -- our local storage is not affected by f
-- attempt to access local storage of dead fiber raises error
pcall(function(f) return f.storage end, f)

--
-- Test that local storage is garbage collected when fiber is died
--
ffi = require('ffi')
ch = fiber.channel(1)
test_run:cmd("setopt delimiter ';'")
function testfun()
    fiber.self().storage.x = ffi.gc(ffi.new('char[1]'),
         function() ch:put('gc ok') end)
end;
test_run:cmd("setopt delimiter ''");
f = fiber.create(testfun)
collectgarbage('collect')
ch:get()
ch:close()
ch = nil



--
-- Test that local storage is not garbage collected with fiber object
--
test_run:cmd("setopt delimiter ';'")
function testfun(ch)
    fiber.self().storage.x = 'ok'
    collectgarbage('collect')
    ch:put(fiber.self().storage.x or 'failed')
end;
test_run:cmd("setopt delimiter ''");
ch = fiber.channel(1)
fiber.create(testfun, ch):status()
ch:get()
ch:close()
ch = nil

-- # gh-125 box.fiber.cancel() by numeric id
--
function y() while true do fiber.sleep(0.001) end end
f = fiber.create(y)
fiber.kill(f:id())
while f:status() ~= 'dead' do fiber.sleep(0.01) end

-- # gh-420 fiber.cancel() assertion `!(f->flags & (1 << 2))' failed
--
done = false
test_run:cmd("setopt delimiter ';'")
function test()
    fiber.name('gh-420')
    local fun, errmsg = loadstring('fiber.cancel(fiber.self())')
    xpcall(fun, function() end)
    xpcall(fun, function() end)
    done = true
    fun()
end;
test_run:cmd("setopt delimiter ''");
f = fiber.create(test)
done

-- # gh-536: fiber.info() doesn't list fibers with default names
--
function loop() while true do fiber.sleep(10) end end
f1 = fiber.create(loop)
f2 = fiber.create(loop)
f3 = fiber.create(loop)

info = fiber.info()
info[f1:id()] ~= nil
info[f2:id()] ~= nil
info[f3:id()] ~= nil

info = fiber.info({bt = false})
info[f1:id()].backtrace == nil
info = fiber.info({backtrace = false})
info[f1:id()].backtrace == nil

f1:cancel()
f2:cancel()
f3:cancel()

-- # gh-666: nulls in output
--
getmetatable(fiber.info())

zombie = false
for fid, i in pairs(fiber.info()) do if i.name == 'zombie' then zombie = true end end
zombie
-- test case for gh-778 - fiber.id() on a dead fiber

f =  fiber.create(function() end)
id = f:id()
fiber.sleep(0)
f:status() 
id == f:id()

--
-- gh-1238: log error if a fiber terminates due to uncaught Lua error
--

-- must show in the log
_ = fiber.create(function() error('gh-1238') end)
test_run:grep_log("default", "gh%-1238") ~= nil

-- must NOT show in the log
_ = fiber.create(function() fiber.self():cancel() end)
fiber.sleep(0.001)
test_run:grep_log("default", "FiberIsCancelled") == nil

-- must show in the log
_ = fiber.create(function() box.error(box.error.ILLEGAL_PARAMS, 'oh my') end)
test_run:grep_log("default", "ER_ILLEGAL_PARAMS:[^\n]*")

-- #1734 fiber.name irt dead fibers
fiber.create(function()end):name()

--
-- gh-1926
--
fiber.create(function() fiber.wakeup(fiber.self()) end)

--
-- gh-2066 test for fiber wakeup
--

_ = box.schema.space.create('test2066', {if_not_exists = true})
_ = box.space.test2066:create_index('pk', {if_not_exists = true})

function fn2() fiber.sleep(60) box.space.test2066:replace({1}) end
f2 = fiber.create(fn2)

function fn1() fiber.sleep(60) f2:wakeup() end
f1 = fiber.create(fn1)
-- push two fibers to ready list
f1:wakeup() f2:wakeup()

fiber.sleep(0.01)

box.space.test2066:drop()

--
-- gh-2642 box.session.type()
--
session_type = ""
function fn1() session_type = box.session.type() return end
_ = fiber.create(fn1)
session_type
session_type = nil

-- gh-1397 fiber.new, fiber.join
test_run:cmd("setopt delimiter ';'")
function err() box.error(box.error.ILLEGAL_PARAMS, 'oh my') end;
function test1()
    f = fiber.new(err)
    f:set_joinable(true)
    local st, e = f:join()
    return st, e
end;
st, e = test1();
st;
e:unpack();

flag = false;
function test2()
    f = fiber.new(function() flag = true  end)
    fiber.set_joinable(f, true)
    fiber.join(f)
end;
test2();
flag;

function test3()
    f = fiber.new(function() return "hello" end)
    fiber.set_joinable(f, true)
    return fiber.join(f)
end;
test3();

function test4()
    f = fiber.new(function (i) return i + 1  end, 1)
    fiber.set_joinable(f, true)
    return f:join()
end;
test4();

function test_double_join()
    f = fiber.new(function (i) return i + 1  end, 1)
    fiber.set_joinable(f, true)
    f:join()
    return f:join()
end;
test_double_join();


function test5()
    f = fiber.new(function() end)
    f:set_joinable(true)
    return f, f:status()
end;
local status;
f, status = test5();
status;
f:status();
f:join();
f:status();

function test6()
    f = fiber.new(function() end)
    f:set_joinable(true)
    f:set_joinable(false)
    return f, f:status()
end;
f, status = test6();
status;
f:status();

-- test side fiber in transaction
s = box.schema.space.create("test");
_ = s:create_index("prim", {parts={1, 'number'}});
flag = false;
function test7(i)
    box.begin()
    s:put{i}
    fiber.new(function(inc) s:put{inc + 1} flag = true end, i)
    box.rollback()
end;
f = fiber.create(test7, 1);
while flag ~= true do fiber.sleep(0.001) end;
s:select{};
s:drop();
test_run:cmd("setopt delimiter ''");
fiber = nil

--
-- gh-2622 fiber.name() truncates new name
--
fiber = require('fiber')
long_name = string.rep('a', 300)
fiber.name()
fiber.name('new_name')
fiber.name(long_name)
fiber.name()
fiber.name(long_name, {truncate = true})
fiber.name()
f = fiber.self()
fiber.name(f)
fiber.name(f, 'new_name')
fiber.name(f, long_name)
fiber.name(f)
fiber.name(f, long_name, {truncate = true})
fiber.name(f)

test_run:cmd("clear filter")
