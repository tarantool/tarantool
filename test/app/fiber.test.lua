fiber = require('fiber')
space = box.schema.space.create('tweedledum')
index = space:create_index('primary', { type = 'hash' })
env = require('test_run')
test_run = env.new()

-- A test case for a race condition between ev_schedule
-- and wal_schedule fiber schedulers.
-- The same fiber should not be scheduled by ev_schedule (e.g.
-- due to cancellation) if it is within th wal_schedule queue.
-- The test case is dependent on wal_max_size, since this is when
-- we reopen the .xlog file and thus wal_scheduler takes a long
-- pause
box.cfg.wal_max_size
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
f = dofile("fiber.lua")
-- print run fiber's test
f()
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

function sf1() loop() end
function sf2() sf1() end
function sf3() sf2() end
f1 = fiber.create(sf3)

info = fiber.info()
-- if compiled without libunwind support, need to return mock object here
-- to skip this test, see gh-3824
backtrace = info[f1:id()].backtrace or {{L = 'sf1'}, {L = 'loop'}, {L = 'sf3'}}
bt_str = ''
for _, b in pairs(backtrace) do bt_str = bt_str .. (b['L'] or '') end
bt_str:find('sf1') ~= nil
bt_str:find('loop') ~= nil
bt_str:find('sf3') ~= nil

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
test_run:grep_log("default", "IllegalParams:[^\n]*")

-- #1734 fiber.name irt dead fibers
fiber.create(function()end):name()

--
-- gh-1926
--
_ = fiber.create(function() fiber.wakeup(fiber.self()) end)

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
test_run:wait_cond(function()
    status = f:status()
    return status == 'dead', status
end, 10);

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
-- gh-2622, gh-4011, gh-4394: fiber.name() truncates new name.
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

--
-- gh-3493 fiber.create() does not roll back memtx transaction
--
_ = box.schema.space.create('test')
_ = box.space.test:create_index('pk')
--
-- Check that derived fiber does not see changes of the transaction
-- it must be rolled back before call
--
l = nil
test_run:cmd("setopt delimiter ';'")
box.begin()
    box.space.test:insert{1}
    f = fiber.create(function() l = box.space.test:get{1} end)
box.rollback();
test_run:cmd("setopt delimiter ''");
while f:status() ~= 'dead' do fiber.sleep(0.01) end
l
f = nil
l = nil
box.space.test:drop()
--
-- Check that yield trigger is installed for a sub-statement
-- in autocommit mode
--
_ = box.schema.space.create('test')
_ = box.space.test:create_index('pk')
test_run:cmd("setopt delimiter ';'")
l = nil
function yield()
    f = fiber.create(function() l = box.space.test:get{1} end)
    while f:status() ~= 'dead' do fiber.sleep(0.01) end
end;
test_run:cmd("setopt delimiter ''");
_ = box.space.test:on_replace(yield)
box.space.test:insert{1}
l
box.space.test:get{1}
f = nil
l = nil
box.space.test:drop()
--
-- Check that rollback trigger is not left behind in the fiber in
-- case of user rollback.
--
-- Begin a multi-statement transaction in memtx and roll it back.
-- Then begin a multi-statement transaction in vinyl and yield.
-- Observe vinyl transaction being implicitly rolled back by
-- yield.
--
_ = box.schema.space.create('m')
_ = box.space.m:create_index('pk')
_ = box.schema.space.create('v', {engine='vinyl'})
_ = box.space.v:create_index('pk')
l = nil
l1 = nil
test_run:cmd("setopt delimiter ';'")
function f()
    box.begin()
    box.space.m:insert{1}
    box.rollback()
    box.begin()
    box.space.v:insert{1}
    local f = fiber.create(function() l = box.space.v:get{1} end)
    while f:status() ~= 'dead' do
       fiber.sleep(0.01)
    end
    box.commit()
    l1 = box.space.v:get{1}
end;
test_run:cmd("setopt delimiter ''");
f()
l
l1
box.space.m:drop()
box.space.v:drop()
f = nil
l = nil
l1 = nil

-- gh-3948 fiber.join() blocks if fiber is cancelled.
function another_func() ch1:get() end
test_run:cmd("setopt delimiter ';'")
function func()
    local fib = fiber.create(another_func)
    fib:set_joinable(true)
    ch2:put(1)
    fib:join()
end;
test_run:cmd("setopt delimiter ''");

ch1 = fiber.channel(1)
ch2 = fiber.channel(1)

f = fiber.create(func)
ch2:get()
f:cancel()
ch1:put(1)

while f:status() ~= 'dead' do fiber.sleep(0.01) end

--
-- Test if fiber join() does not crash
-- if unjoinable
--
fiber.join(fiber.self())

sum = 0

-- gh-2694 fiber.top()
-- The following checks for `is_fiber_top` and default phony values
-- are a workaround for the problems of diff-based test in case of build
-- without fiber.top.
-- FIXME: fiber.top and backtraces require not diff-based tests.
is_fiber_top = fiber.top ~= nil
if is_fiber_top then fiber.top_enable() end


-- Wait till a full event loop iteration passes, so that
-- top() contains meaningful results. On the ev loop iteration
-- following fiber.top_enable() results will be zero.
if is_fiber_top then\
    while fiber.top().cpu["1/sched"].instant == 0 do fiber.yield() end\
end

a = is_fiber_top and fiber.top() or {cpu = {["1/sched"] = {}}, cpu_misses = 0}
type(a) == 'table'
-- scheduler is present in fiber.top()
-- and is indexed by name
a.cpu["1/sched"] ~= nil
a.cpu_misses == 0
sum_inst = 0
sum_avg = is_fiber_top and 0 or 100

-- update table to make sure
-- a full event loop iteration
-- has ended
if is_fiber_top then\
    a = fiber.top().cpu\
    for k, v in pairs(a) do\
        sum_inst = sum_inst + v["instant"]\
        sum_avg = sum_avg + v["average"]\
    end\
end

-- when a fiber dies, its impact on the thread moving average
-- persists for a couple of ev loop iterations, but it is no
-- longer listed in fiber.top(). So sum_avg may way smaller than
-- 100%. See gh-4625 for details and reenable both tests below as
-- soon as it is implemented.
-- In rare cases when a fiber dies on the same event loop
-- iteration as you issue fiber.top(), sum_inst will also be
-- smaller than 100%.
-- sum_inst
sum_avg <= 100.1 or sum_avg
-- not exact due to accumulated integer division errors
--sum_avg > 99 and sum_avg <= 100.1 or sum_avg
tbl = is_fiber_top and {} or {average = 1, instant = 1, time = 1}
if is_fiber_top then\
    f = fiber.new(function()\
        local fiber_key = fiber.self().id()..'/'..fiber.self().name()\
        tbl = fiber.top().cpu[fiber_key]\
        while tbl.time == 0 do\
            for i = 1,1000 do end\
            fiber.yield()\
            tbl = fiber.top().cpu[fiber_key]\
        end\
    end)\
    while f:status() ~= 'dead' do fiber.yield() end\
end
tbl.average > 0
tbl.instant > 0
tbl.time > 0

if is_fiber_top then fiber.top_disable() end
pcall(fiber.top) == false

--
-- fiber._internal.schedule_task() - API for internal usage for
-- delayed execution of a function.
--
glob_arg = {}
count = 0
function task_f(arg)                                                            \
    count = count + 1                                                           \
    table.insert(glob_arg, arg)                                                 \
    arg = arg + 1                                                               \
    if arg <= 3 then                                                            \
        fiber._internal.schedule_task(task_f, arg)                              \
    else                                                                        \
        error('Worker is broken')                                               \
    end                                                                         \
end
for i = 1, 3 do                                                                 \
    local csw1 = fiber.info()[fiber.id()].csw                                   \
    fiber._internal.schedule_task(task_f, i)                                    \
    local csw2 = fiber.info()[fiber.id()].csw                                   \
    assert(csw1 == csw2 and type(csw1) == 'number')                             \
end
old_count = count
test_run:wait_cond(function()                                                   \
    fiber.yield()                                                               \
    if count == old_count then                                                  \
        return true                                                             \
    end                                                                         \
    old_count = count                                                           \
end)
glob_arg
count

-- Ensure, that after all tasks are finished, the worker didn't
-- stuck somewhere.
glob_arg = nil
fiber._internal.schedule_task(function(arg) glob_arg = arg end, 100)
fiber.yield()
glob_arg

-- cleanup
test_run:cmd("clear filter")

box.schema.user.grant('guest', 'execute', 'universe')
con = require('net.box').connect(box.cfg.listen)
pcall(con.eval, con, 'fiber.cancel(fiber.self())')
con:eval('fiber.sleep(0) return "Ok"')
box.schema.user.revoke('guest', 'execute', 'universe')
