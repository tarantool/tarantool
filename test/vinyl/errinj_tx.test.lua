test_run = require('test_run').new()
fiber = require('fiber')
txn_proxy = require('txn_proxy')
create_iterator = require('utils').create_iterator
errinj = box.error.injection

--
-- gh-1681: vinyl: crash in vy_rollback on ER_WAL_WRITE
--
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
-- Check that dependent transaction is aborted on WAL write.
--
-- gh-4070: an aborted transaction must fail any DML/DQL request.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
s:replace{10}

-- Insert a tuple into the memory level and stall on WAL write.
ch = fiber.channel(1)
errinj.set('ERRINJ_WAL_DELAY', true)
_ = fiber.create(function() pcall(s.replace, s, {1}) ch:put(true) end)

-- Read the tuple from another transaction.
itr = create_iterator(s)
itr.next()

c = txn_proxy.new()
c:begin()
c('s:get(1)')

-- Resume the first transaction and let it fail on WAL write.
errinj.set('ERRINJ_WAL_WRITE', true)
errinj.set('ERRINJ_WAL_DELAY', false)
ch:get()
errinj.set('ERRINJ_WAL_WRITE', false)

-- Must fail.
itr.next()
c('s:get(1)')
c('s:select()')
c('s:replace{1}')
c:commit()

itr = nil
s:drop()

-- Collect all iterators to make sure no read views are left behind,
-- as they might disrupt the following test run.
collectgarbage()
box.stat.vinyl().tx.read_views -- 0
