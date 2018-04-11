--
-- gh-1681: vinyl: crash in vy_rollback on ER_WAL_WRITE
--
test_run = require('test_run').new()
fiber = require('fiber')
errinj = box.error.injection
errinj.set("ERRINJ_VY_SCHED_TIMEOUT", 0.040)
s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk')
function f() box.begin() s:insert{1, 'hi'} s:insert{2, 'bye'} box.commit() end
errinj.set("ERRINJ_WAL_WRITE", true)
f()
s:select{}
errinj.set("ERRINJ_WAL_WRITE", false)
f()
s:select{}
s:drop()
--
-- Lost data in case of dump error
--
--
test_run:cmd("setopt delimiter ';'")
if  box.cfg.vinyl_page_size > 1024 or box.cfg.vinyl_range_size > 65536 then
    error("This test relies on splits and dumps")
end;
s = box.schema.space.create('test', {engine='vinyl'});
_ = s:create_index('pk');
value = string.rep('a', 1024)
last_id = 1
-- fill up a range
function range()
    local range_size = box.cfg.vinyl_range_size
    local page_size = box.cfg.vinyl_page_size
    local s = box.space.test
    local num_rows = 0
    for i=1,range_size/page_size do
        for j=1, page_size/#value do
            s:replace({last_id, value})
            last_id = last_id + 1
            num_rows = num_rows + 1
        end
    end
    return num_rows
end;
num_rows = 0;
num_rows = num_rows + range();
box.snapshot();
errinj.set("ERRINJ_VY_RUN_WRITE", true);
num_rows = num_rows + range();
-- fails due to error injection
box.snapshot();
errinj.set("ERRINJ_VY_RUN_WRITE", false);
-- fails due to scheduler timeout
box.snapshot();
fiber.sleep(0.06);
num_rows = num_rows + range();
box.snapshot();
num_rows = num_rows + range();
box.snapshot();
num_rows;
for i=1,num_rows do
    if s:get{i} == nil then
        error("Row "..i.."not found")
    end
end;
#s:select{} == num_rows;
s:drop();
test_run:cmd("setopt delimiter ''");

-- Disable the cache so that we can check that disk errors
-- are handled properly.
vinyl_cache = box.cfg.vinyl_cache
box.cfg{vinyl_cache = 0}

s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk')
for i = 1, 10 do s:insert({i, 'test str' .. tostring(i)}) end
box.snapshot()
s:select()
errinj.set("ERRINJ_VY_READ_PAGE", true)
s:select()
errinj.set("ERRINJ_VY_READ_PAGE", false)
s:select()

errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", 0.05)
function test_cancel_read () k = s:select() return #k end
f1 = fiber.create(test_cancel_read)
fiber.cancel(f1)
-- task should be done
fiber.sleep(0.1)
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", 0);
s:select()

-- error after timeout for canceled fiber
errinj.set("ERRINJ_VY_READ_PAGE", true)
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", 0.05)
f1 = fiber.create(test_cancel_read)
fiber.cancel(f1)
fiber.sleep(0.1)
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", 0);
errinj.set("ERRINJ_VY_READ_PAGE", false);
s:select()

-- index is dropped while a read task is in progress
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", 0.05)
f1 = fiber.create(test_cancel_read)
fiber.cancel(f1)
s:drop()
fiber.sleep(0.1)
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", 0);

box.cfg{vinyl_cache = vinyl_cache}

-- gh-2871: check that long reads are logged
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
for i = 1, 10 do s:insert{i, i * 2} end
box.snapshot()
too_long_threshold = box.cfg.too_long_threshold
box.cfg{too_long_threshold = 0.01}
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", 0.05)
s:get(10) ~= nil
#s:select(5, {iterator = 'LE'}) == 5
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", 0);
test_run:cmd("push filter 'lsn=[0-9]+' to 'lsn=<lsn>'")
test_run:grep_log('default', 'get.* took too long')
test_run:grep_log('default', 'select.* took too long')
test_run:cmd("clear filter")
box.cfg{too_long_threshold = too_long_threshold}

s:drop()

s = box.schema.space.create('test', {engine='vinyl'});
_ = s:create_index('pk');
_ = s:replace({1, string.rep('a', 128000)})
errinj.set("ERRINJ_WAL_WRITE_DISK", true)
box.snapshot()
errinj.set("ERRINJ_WAL_WRITE_DISK", false)
fiber.sleep(0.06)
_ = s:replace({2, string.rep('b', 128000)})
box.snapshot();
#s:select({1})
s:drop()

errinj.set("ERRINJ_VY_SCHED_TIMEOUT", 0)

--
-- Check that upsert squash fiber does not crash if index or
-- in-memory tree is gone.
--
errinj.set("ERRINJ_VY_SQUASH_TIMEOUT", 0.050)
s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk')
s:insert{0, 0}
box.snapshot()
for i=1,256 do s:upsert({0, 0}, {{'+', 2, 1}}) end
box.snapshot() -- in-memory tree is gone
fiber.sleep(0.05)
s:select()
s:replace{0, 0}
box.snapshot()
for i=1,256 do s:upsert({0, 0}, {{'+', 2, 1}}) end
s:drop() -- index is gone
fiber.sleep(0.05)
errinj.set("ERRINJ_VY_SQUASH_TIMEOUT", 0)

--https://github.com/tarantool/tarantool/issues/1842
--test error injection
s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk')
s:replace{0, 0}

s:replace{1, 0}
s:replace{2, 0}
errinj.set("ERRINJ_WAL_WRITE", true)
s:replace{3, 0}
s:replace{4, 0}
s:replace{5, 0}
s:replace{6, 0}
errinj.set("ERRINJ_WAL_WRITE", false)
s:replace{7, 0}
s:replace{8, 0}
s:select{}

s:drop()

create_iterator = require('utils').create_iterator

--iterator test
test_run:cmd("setopt delimiter ';'")

fiber_status = 0

function fiber_func()
    box.begin()
    s:replace{5, 5}
    fiber_status = 1
    local res = {pcall(box.commit) }
    fiber_status = 2
    return unpack(res)
end;

test_run:cmd("setopt delimiter ''");

s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk')
fiber = require('fiber')

_ = s:replace{0, 0}
_ = s:replace{10, 0}
_ = s:replace{20, 0}

test_run:cmd("setopt delimiter ';'");

faced_trash = false
for i = 1,100 do
    errinj.set("ERRINJ_WAL_WRITE", true)
    local f = fiber.create(fiber_func)
    local itr = create_iterator(s, {0}, {iterator='GE'})
    local first = itr.next()
    local second = itr.next()
    if (second[1] ~= 5 and second[1] ~= 10) then faced_trash = true end
    while fiber_status <= 1 do fiber.sleep(0.001) end
    local _,next = pcall(itr.next)
    _,next = pcall(itr.next)
    _,next = pcall(itr.next)
    errinj.set("ERRINJ_WAL_WRITE", false)
    s:delete{5}
end;

test_run:cmd("setopt delimiter ''");

faced_trash

s:drop()

-- TX in prepared but not committed state
s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk')
fiber = require('fiber')
txn_proxy = require('txn_proxy')

s:replace{1, "original"}
s:replace{2, "original"}
s:replace{3, "original"}

c0 = txn_proxy.new()
c0:begin()
c1 = txn_proxy.new()
c1:begin()
c2 = txn_proxy.new()
c2:begin()
c3 = txn_proxy.new()
c3:begin()

--
-- Prepared transactions
--

-- Pause WAL writer to cause all further calls to box.commit() to move
-- transactions into prepared, but not committed yet state.
errinj.set("ERRINJ_WAL_DELAY", true)
lsn = box.info.lsn
c0('s:replace{1, "c0"}')
c0('s:replace{2, "c0"}')
c0('s:replace{3, "c0"}')
_ = fiber.create(c0.commit, c0)
box.info.lsn == lsn
c1('s:replace{1, "c1"}')
c1('s:replace{2, "c1"}')
_ = fiber.create(c1.commit, c1)
box.info.lsn == lsn
c3('s:select{1}') -- c1 is visible
c2('s:replace{1, "c2"}')
c2('s:replace{3, "c2"}')
_ = fiber.create(c2.commit, c2)
box.info.lsn == lsn
c3('s:select{1}') -- c1 is visible, c2 is not
c3('s:select{2}') -- c1 is visible
c3('s:select{3}') -- c2 is not visible

-- Resume WAL writer and wait until all transactions will been committed
errinj.set("ERRINJ_WAL_DELAY", false)
REQ_COUNT = 7
while box.info.lsn - lsn < REQ_COUNT do fiber.sleep(0.01) end
box.info.lsn == lsn + REQ_COUNT

c3('s:select{1}') -- c1 is visible, c2 is not
c3('s:select{2}') -- c1 is visible
c3('s:select{3}') -- c2 is not visible
c3:commit()

s:drop()

--
-- Test mem restoration on a prepared and not commited statement
-- after moving iterator into read view.
--
space = box.schema.space.create('test', {engine = 'vinyl'})
pk = space:create_index('pk')
space:replace{1}
space:replace{2}
space:replace{3}

last_read = nil

errinj.set("ERRINJ_WAL_DELAY", true)

test_run:cmd("setopt delimiter ';'")

function fill_space()
    box.begin()
    space:replace{1}
    space:replace{2}
    space:replace{3}
-- block until wal_delay = false
    box.commit()
-- send iterator to read view
    space:replace{1, 1}
-- flush mem and update index version to trigger iterator restore
    box.snapshot()
end;

function iterate_in_read_view()
    local i = create_iterator(space)
    last_read = i.next()
    fiber.sleep(100000)
    last_read = i.next()
end;

test_run:cmd("setopt delimiter ''");

f1 = fiber.create(fill_space)
-- Prepared transaction is blocked due to wal_delay.
-- Start iterator with vlsn = INT64_MAX
f2 = fiber.create(iterate_in_read_view)
last_read
-- Finish prepared transaction and send to read view the iterator.
errinj.set("ERRINJ_WAL_DELAY", false)
while f1:status() ~= 'dead' do fiber.sleep(0.01) end
f2:wakeup()
while f2:status() ~= 'dead' do fiber.sleep(0.01) end
last_read

space:drop()

--
-- Space drop in the middle of dump.
--
test_run:cmd("create server test with script='vinyl/low_quota.lua'")
test_run:cmd("start server test with args='1048576'")
test_run:cmd('switch test')
fiber = require 'fiber'
box.cfg{vinyl_timeout = 0.001}
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('i1', {parts = {1, 'unsigned'}})
_ = s:create_index('i2', {parts = {2, 'unsigned'}})
_ = s:insert{1, 1}
-- Delay dump so that we can manage to drop the space
-- while it is still being dumped.
box.error.injection.set('ERRINJ_VY_RUN_WRITE_TIMEOUT', 0.1)
-- Before failing on quota timeout, the following fiber
-- will trigger dump due to memory shortage.
_ = fiber.create(function() s:insert{2, 2, string.rep('x', box.cfg.vinyl_memory)} end)
-- Let the fiber run.
fiber.sleep(0)
-- Drop the space while the dump task is still running.
s:drop()
-- Wait for the dump task to complete.
box.snapshot()
box.error.injection.set('ERRINJ_VY_RUN_WRITE_TIMEOUT', 0)

--
-- Check that all dump/compact tasks that are in progress at
-- the time when the server stops are aborted immediately.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('i1', {parts = {1, 'unsigned'}})
_ = s:create_index('i2', {parts = {2, 'unsigned'}})
box.error.injection.set('ERRINJ_VY_RUN_WRITE_STMT_TIMEOUT', 0.01)
for i = 1, 1000 do s:replace{i, i} end
_ = fiber.create(function() box.snapshot() end)
fiber.sleep(0.01)
test_run:cmd('switch default')
t1 = fiber.time()
test_run:cmd("stop server test")
t2 = fiber.time()
t2 - t1 < 1
test_run:cmd("cleanup server test")

--
-- If we logged an index creation in the metadata log before WAL write,
-- WAL failure would result in leaving the index record in vylog forever.
-- Since we use LSN to identify indexes in vylog, retrying index creation
-- would then lead to a duplicate index id in vylog and hence inability
-- to make a snapshot or recover.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
errinj.set('ERRINJ_WAL_IO', true)
_ = s:create_index('pk')
errinj.set('ERRINJ_WAL_IO', false)
_ = s:create_index('pk')
box.snapshot()
s:drop()

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('i1', {parts = {1, 'unsigned'}})

c = 10
errinj.set("ERRINJ_WAL_WRITE_DISK", true)
for i = 1,10 do fiber.create(function() pcall(s.replace, s, {i}) c = c - 1 end) end
while c ~= 0 do fiber.sleep(0.001) end
s:select{}
errinj.set("ERRINJ_WAL_WRITE_DISK", false)

s:drop()

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('i1', {parts = {1, 'unsigned'}})
for i = 0, 9 do s:replace({i, i + 1}) end
box.snapshot()
errinj.set("ERRINJ_XLOG_GARBAGE", true)
s:select()
errinj.set("ERRINJ_XLOG_GARBAGE", false)
errinj.set("ERRINJ_VYRUN_DATA_READ", true)
s:select()
errinj.set("ERRINJ_VYRUN_DATA_READ", false)
s:select()
s:drop()

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('i1', {parts = {1, 'unsigned'}})
for i = 0, 9 do s:replace({i, i + 1}) end
errinj.set("ERRINJ_XLOG_GARBAGE", true)
box.snapshot()
for i = 10, 19 do s:replace({i, i + 1}) end
errinj.set("ERRINJ_XLOG_GARBAGE", false)
box.snapshot()
s:select()

s:drop()

-- Point select from secondary index during snapshot.
-- Once upon time that leaded to crash.
s = box.schema.space.create('test', {engine = 'vinyl'})
i1 = s:create_index('pk', {parts = {1, 'uint'}, bloom_fpr = 0.5})
i2 = s:create_index('sk', {parts = {2, 'uint'}, bloom_fpr = 0.5})

for i = 1,10 do s:replace{i, i, 0} end

test_run:cmd("setopt delimiter ';'")
function worker()
    for i = 11,20,2 do
        s:upsert({i, i}, {{'=', 3, 1}})
        errinj.set("ERRINJ_VY_POINT_ITER_WAIT", true)
        i1:select{i}
        s:upsert({i + 1 ,i + 1}, {{'=', 3, 1}})
        errinj.set("ERRINJ_VY_POINT_ITER_WAIT", true)
        i2:select{i + 1}
    end
end
test_run:cmd("setopt delimiter ''");

f = fiber.create(worker)

while f:status() ~= 'dead' do box.snapshot() fiber.sleep(0.01) end

errinj.set("ERRINJ_VY_POINT_ITER_WAIT", false)
s:drop()

-- vinyl: vy_cache_add: Assertion `0' failed
-- https://github.com/tarantool/tarantool/issues/2685
s = box.schema.create_space('test', {engine = 'vinyl'})
pk = s:create_index('pk')
s:replace{2, 0}
box.snapshot()
s:replace{1, 0}
box.snapshot()
s:replace{0, 0}
s:select{0}

errinj.set("ERRINJ_WAL_DELAY", true)
wait_replace = true
_ = fiber.create(function() s:replace{1, 1} wait_replace = false end)
gen,param,state = s:pairs({1}, {iterator = 'GE'})
state, value = gen(param, state)
value
errinj.set("ERRINJ_WAL_DELAY", false)
while wait_replace do fiber.sleep(0.01) end
state, value = gen(param, state)
value
s:drop()

--
-- gh-2442: secondary index cursor must skip key update, made
-- after the secondary index scan, but before a primary index
-- lookup. It is ok, and the test checks this.
--
s = box.schema.create_space('test', {engine = 'vinyl'})
pk = s:create_index('pk')
sk = s:create_index('sk', {parts = {{2, 'unsigned'}}})
s:replace{1, 1}
s:replace{3, 3}
box.snapshot()
ret = nil
function do_read() ret = sk:select({2}, {iterator = 'GE'}) end
errinj.set("ERRINJ_VY_DELAY_PK_LOOKUP", true)
f = fiber.create(do_read)
f:status()
ret
s:replace{2, 2}
errinj.set("ERRINJ_VY_DELAY_PK_LOOKUP", false)
while ret == nil do fiber.sleep(0.01) end
ret
s:drop()

--
-- Check that ALTER is abroted if a tuple inserted during space
-- format change does not conform to the new format.
--
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'string', is_nullable = true}
s = box.schema.space.create('test', {engine = 'vinyl', format = format})
_ = s:create_index('pk', {page_size = 16})

pad = string.rep('x', 16)
for i = 101, 200 do s:replace{i, pad} end
box.snapshot()

ch = fiber.channel(1)
test_run:cmd("setopt delimiter ';'")
_ = fiber.create(function()
    fiber.sleep(0.01)
    for i = 1, 100 do
        s:replace{i, box.NULL}
    end
    ch:put(true)
end);
test_run:cmd("setopt delimiter ''");

errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", 0.001)
format[2].is_nullable = false
s:format(format) -- must fail
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", 0)

ch:get()

s:count() -- 200
s:drop()
