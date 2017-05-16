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

s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk')
for i = 1, 10 do s:insert({i, 'test str' .. tostring(i)}) end
box.snapshot()
s:select()
errinj.set("ERRINJ_VY_READ_PAGE", true)
s:select()
errinj.set("ERRINJ_VY_READ_PAGE", false)
s:select()

errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", true)
function test_cancel_read () k = s:select() return #k end
f1 = fiber.create(test_cancel_read)
fiber.cancel(f1)
-- task should be done
fiber.sleep(0.1)
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", false);
s:select()

-- error after timeout for canceled fiber
errinj.set("ERRINJ_VY_READ_PAGE", true)
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", true)
f1 = fiber.create(test_cancel_read)
fiber.cancel(f1)
fiber.sleep(0.1)
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", false);
errinj.set("ERRINJ_VY_READ_PAGE", false);
s:select()
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

--iterator test
test_run:cmd("setopt delimiter ';'")

function create_iterator(obj, key, opts)
    local iter, key, state = obj:pairs(key, opts)
    local res = {}
    res['iter'] = iter
    res['key'] = key
    res['state'] = state
    return res
end;

function iterator_next(iter_obj)
    local st, tp = iter_obj.iter.gen(iter_obj.key, iter_obj.state)
    return tp
end;

function iterate_over(iter_obj)
    local tp = nil
    local ret = {}
    local i = 0
    tp = iterator_next(iter_obj)
    while tp do
        ret[i] = tp
        i = i + 1
        tp = iterator_next(iter_obj)
    end
    return ret
end;

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
    local first = iterator_next(itr)
    local second = iterator_next(itr)
    if (second[1] ~= 5 and second[1] ~= 10) then faced_trash = true end
    while fiber_status <= 1 do fiber.sleep(0.001) end
    local _,next = pcall(iterator_next, itr)
    _,next = pcall(iterator_next, itr)
    _,next = pcall(iterator_next, itr)
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
lsn = box.info.vinyl().lsn
c0('s:replace{1, "c0"}')
c0('s:replace{2, "c0"}')
c0('s:replace{3, "c0"}')
_ = fiber.create(c0.commit, c0)
box.info.vinyl().lsn == lsn
c1('s:replace{1, "c1"}')
c1('s:replace{2, "c1"}')
_ = fiber.create(c1.commit, c1)
box.info.vinyl().lsn == lsn
c3('s:select{1}') -- c1 is visible
c2('s:replace{1, "c2"}')
c2('s:replace{3, "c2"}')
_ = fiber.create(c2.commit, c2)
box.info.vinyl().lsn == lsn
c3('s:select{1}') -- c1 is visible, c2 is not
c3('s:select{2}') -- c1 is visible
c3('s:select{3}') -- c2 is not visible

-- Resume WAL writer and wait until all transactions will been committed
errinj.set("ERRINJ_WAL_DELAY", false)
REQ_COUNT = 7
while box.info.vinyl().lsn - lsn < REQ_COUNT do fiber.sleep(0.01) end
box.info.vinyl().lsn == lsn + REQ_COUNT

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
    last_read = iterator_next(i)
    fiber.sleep(100000)
    last_read = iterator_next(i)
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
