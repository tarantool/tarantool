-- Test that recovery had been completed without errors
test_run = require('test_run').new()
test_run:cmd("restart server default")
box.error.last() == nil

errinj = box.error.injection
net_box = require('net.box')

space = box.schema.space.create('tweedledum')
index = space:create_index('primary', { type = 'hash' })

--
-- Print all error keys in sorted order to minimize diff output
-- when new ones are merged in.
ekeys = {}
evals = {}
for k, v in pairs(errinj.info()) do \
    table.insert(ekeys, k)          \
end
table.sort(ekeys)
for i, k in ipairs(ekeys) do         \
    evals[i] = {[k] = errinj.get(k)} \
end
evals

errinj.set("some-injection", true)
errinj.set("some-injection") -- check error
space:select{222444}
errinj.set("ERRINJ_TESTING", true)
space:select{222444}
errinj.set("ERRINJ_TESTING", false)

-- Check how well we handle a failed log write
errinj.set("ERRINJ_WAL_IO", true)
space:insert{1}
space:get{1}
errinj.set("ERRINJ_WAL_IO", false)
space:insert{1}
errinj.set("ERRINJ_WAL_IO", true)
space:update(1, {{'=', 2, 2}})
space:get{1}
space:get{2}
errinj.set("ERRINJ_WAL_IO", false)
space:update(1, {{'=', 2, 2}})
space:truncate()

-- Check that WAL vclock isn't promoted on failed write.
lsn1 = box.info.vclock[box.info.id]
errinj.set("ERRINJ_WAL_WRITE_PARTIAL", 0)
space:insert{1}
errinj.set("ERRINJ_WAL_WRITE_PARTIAL", -1)
space:insert{1}
-- Check vclock was promoted only one time
box.info.vclock[box.info.id] == lsn1 + 1
errinj.set("ERRINJ_WAL_WRITE_PARTIAL", 0)
space:update(1, {{'=', 2, 2}})
space:get{1}
errinj.set("ERRINJ_WAL_WRITE_PARTIAL", -1)
space:update(1, {{'=', 2, 2}})
-- Check vclock was promoted only two times
box.info.vclock[box.info.id] == lsn1 + 2
space:truncate()

-- Check a failed log rotation
errinj.set("ERRINJ_WAL_ROTATE", true)
space:insert{1}
space:get{1}
errinj.set("ERRINJ_WAL_ROTATE", false)
space:insert{1}
errinj.set("ERRINJ_WAL_ROTATE", true)
space:update(1, {{'=', 2, 2}})
space:get{1}
space:get{2}
errinj.set("ERRINJ_WAL_ROTATE", false)
space:update(1, {{'=', 2, 2}})
space:get{1}
space:get{2}
space:truncate()

space:drop()

-- Check how well we handle a failed log write in DDL
s_disabled = box.schema.space.create('disabled')
s_withindex = box.schema.space.create('withindex')
index1 = s_withindex:create_index('primary', { type = 'hash' })
s_withdata = box.schema.space.create('withdata')
index2 = s_withdata:create_index('primary', { type = 'tree' })
s_withdata:insert{1, 2, 3, 4, 5}
s_withdata:insert{4, 5, 6, 7, 8}
index3 = s_withdata:create_index('secondary', { type = 'hash', parts = {2, 'unsigned', 3, 'unsigned' }})
errinj.set("ERRINJ_WAL_IO", true)
test = box.schema.space.create('test')
s_disabled:create_index('primary', { type = 'hash' })
s_disabled.enabled
s_disabled:insert{0}
s_withindex:create_index('secondary', { type = 'tree', parts = { 2, 'unsigned'} })
s_withindex.index.secondary
s_withdata.index.secondary:drop()
s_withdata.index.secondary.unique
s_withdata:drop()
box.space['withdata'].enabled
index4 = s_withdata:create_index('another', { type = 'tree', parts = { 5, 'unsigned' }, unique = false})
s_withdata.index.another
errinj.set("ERRINJ_WAL_IO", false)
test = box.schema.space.create('test')
index5 = s_disabled:create_index('primary', { type = 'hash' })
s_disabled.enabled
s_disabled:insert{0}
index6 = s_withindex:create_index('secondary', { type = 'tree', parts = { 2, 'unsigned'} })
s_withindex.index.secondary.unique
s_withdata.index.secondary:drop()
s_withdata.index.secondary
s_withdata:drop()
box.space['withdata']
index7 = s_withdata:create_index('another', { type = 'tree', parts = { 5, 'unsigned' }, unique = false})
s_withdata.index.another
test:drop()
s_disabled:drop()
s_withindex:drop()

-- Check transaction rollback when out of memory
env = require('test_run')
test_run = env.new()

s = box.schema.space.create('s')
_ = s:create_index('pk')
errinj.set("ERRINJ_TUPLE_ALLOC", true)
s:auto_increment{}
s:select{}
s:auto_increment{}
s:select{}
s:auto_increment{}
s:select{}
test_run:cmd("setopt delimiter ';'")
box.begin()
    s:insert{1}
box.commit();
box.rollback();
s:select{};
box.begin()
    s:insert{1}
    s:insert{2}
box.commit();
s:select{};
box.rollback();
box.begin()
    pcall(s.insert, s, {1})
    s:insert{2}
box.commit();
s:select{};
box.rollback();
errinj.set("ERRINJ_TUPLE_ALLOC", false);
box.begin()
    s:insert{1}
    errinj.set("ERRINJ_TUPLE_ALLOC", true)
    s:insert{2}
box.commit();
errinj.set("ERRINJ_TUPLE_ALLOC", false);
box.rollback();
s:select{};
box.begin()
    s:insert{1}
    errinj.set("ERRINJ_TUPLE_ALLOC", true)
    pcall(s.insert, s, {2})
box.commit();
s:select{};
box.rollback();

test_run:cmd("setopt delimiter ''");
errinj.set("ERRINJ_TUPLE_ALLOC", false)

s:drop()
s = box.schema.space.create('test')
_ = s:create_index('test', {parts = {1, 'unsigned', 3, 'unsigned', 5, 'unsigned'}})
s:insert{1, 2, 3, 4, 5, 6}
t = s:select{}[1]
errinj.set("ERRINJ_TUPLE_FIELD", true)
tostring(t[1]) .. tostring(t[2]) ..tostring(t[3]) .. tostring(t[4]) .. tostring(t[5]) .. tostring(t[6])
errinj.set("ERRINJ_TUPLE_FIELD", false)
tostring(t[1]) .. tostring(t[2]) ..tostring(t[3]) .. tostring(t[4]) .. tostring(t[5]) .. tostring(t[6])

s:drop()
s = box.schema.space.create('test')
_ = s:create_index('test', {parts = {2, 'unsigned', 4, 'unsigned', 6, 'unsigned'}})
s:insert{1, 2, 3, 4, 5, 6}
t = s:select{}[1]
errinj.set("ERRINJ_TUPLE_FIELD", true)
tostring(t[1]) .. tostring(t[2]) ..tostring(t[3]) .. tostring(t[4]) .. tostring(t[5]) .. tostring(t[6])
errinj.set("ERRINJ_TUPLE_FIELD", false)
tostring(t[1]) .. tostring(t[2]) ..tostring(t[3]) .. tostring(t[4]) .. tostring(t[5]) .. tostring(t[6])

-- Cleanup
s:drop()

--
-- gh-2046: don't store offsets for sequential multi-parts keys
--
s = box.schema.space.create('test')
_ = s:create_index('seq2', { parts = { 1, 'unsigned', 2, 'unsigned' }})
_ = s:create_index('seq3', { parts = { 1, 'unsigned', 2, 'unsigned', 3, 'unsigned' }})
_ = s:create_index('seq5', { parts = { 1, 'unsigned', 2, 'unsigned', 3, 'unsigned', 4, 'scalar', 5, 'number' }})
_ = s:create_index('rnd1', { parts = { 3, 'unsigned' }})

errinj.set("ERRINJ_TUPLE_FIELD", true)
tuple = s:insert({1, 2, 3, 4, 5, 6, 7, 8, 9, 10})
tuple
tuple[1] -- not-null, always accessible
tuple[2] -- null, doesn't have offset
tuple[3] -- not null, has offset
tuple[4] -- null, doesn't have offset
tuple[5] -- null, doesn't have offset
s.index.seq2:select({1})
s.index.seq2:select({1, 2})
s.index.seq3:select({1})
s.index.seq3:select({1, 2, 3})
s.index.seq5:select({1})
s.index.seq5:select({1, 2, 3, 4, 5})
s.index.rnd1:select({3})
errinj.set("ERRINJ_TUPLE_FIELD", false)
s:drop()

space = box.schema.space.create('test')
_ = space:create_index('pk')
errinj.set("ERRINJ_WAL_WRITE", true)
space:insert{1}
errinj.set("ERRINJ_WAL_WRITE", false)

errinj.set("ERRINJ_WAL_WRITE_DISK", true)
_ = space:insert{1, require'digest'.urandom(192 * 1024)}
errinj.set("ERRINJ_WAL_WRITE_DISK", false)

_ = space:insert{1}
errinj.set("ERRINJ_WAL_WRITE", true)
box.snapshot()
errinj.set("ERRINJ_WAL_WRITE", false)
space:drop()

--test space:bsize() in case of memory error
utils = dofile('utils.lua')
s = box.schema.space.create('space_bsize')
idx = s:create_index('primary')

for i = 1, 13 do s:insert{ i, string.rep('x', i) } end

s:bsize()
utils.space_bsize(s)

errinj.set("ERRINJ_TUPLE_ALLOC", true)

s:replace{1, "test"}
s:bsize()
utils.space_bsize(s)

s:update({1}, {{'=', 3, '!'}})
s:bsize()
utils.space_bsize(s)

errinj.set("ERRINJ_TUPLE_ALLOC", false)

s:drop()

space = box.schema.space.create('test')
index1 = space:create_index('primary')
fiber = require'fiber'
ch = fiber.channel(1)

test_run:cmd('setopt delimiter ";"')
function test()
  errinj.set('ERRINJ_WAL_WRITE_DISK', true)
  pcall(box.space.test.replace, box.space.test, {1, 1})
  errinj.set('ERRINJ_WAL_WRITE_DISK', false)
  ch:put(true)
end ;

function run()
  fiber.create(test)
  box.snapshot()
end ;

test_run:cmd('setopt delimiter ""');

-- Port_dump can fail.

box.schema.user.grant('guest', 'read', 'space', '_space')

cn = net_box.connect(box.cfg.listen)
cn:ping()
errinj.set('ERRINJ_PORT_DUMP', true)
ok, ret = pcall(cn.space._space.select, cn.space._space)
assert(not ok)
assert(string.match(tostring(ret), 'Failed to allocate'))
errinj.set('ERRINJ_PORT_DUMP', false)
cn:close()
box.schema.user.revoke('guest', 'read', 'space', '_space')

run()
ch:get()

box.space.test:select()
test_run:cmd('restart server default')
box.space.test:select()
box.space.test:drop()

errinj = box.error.injection
net_box = require('net.box')
fiber = require'fiber'

s = box.schema.space.create('test')
_ = s:create_index('pk')

ch = fiber.channel(2)

test_run:cmd("setopt delimiter ';'")
function test(tuple)
   ch:put({pcall(s.replace, s, tuple)})
end;
test_run:cmd("setopt delimiter ''");

errinj.set("ERRINJ_WAL_WRITE", true)
_ = {fiber.create(test, {1, 2, 3}), fiber.create(test, {3, 4, 5})}

{ch:get(), ch:get()}
errinj.set("ERRINJ_WAL_WRITE", false)
s:drop()

-- rebuild some secondary indexes if the primary was changed
s = box.schema.space.create('test')
i1 = s:create_index('i1', {parts = {1, 'unsigned'}})
--i2 = s:create_index('i2', {parts = {5, 'unsigned'}, unique = false})
--i3 = s:create_index('i3', {parts = {6, 'unsigned'}, unique = false})
i2 = i1 i3 = i1

_ = s:insert{1, 4, 3, 4, 10, 10}
_ = s:insert{2, 3, 1, 2, 10, 10}
_ = s:insert{3, 2, 2, 1, 10, 10}
_ = s:insert{4, 1, 4, 3, 10, 10}

i1:select{}
i2:select{}
i3:select{}

i1:alter({parts={2, 'unsigned'}})

_ = collectgarbage('collect')
i1:select{}
i2:select{}
i3:select{}

box.error.injection.set('ERRINJ_BUILD_INDEX', i2.id)

i1:alter{parts = {3, "unsigned"}}

_ = collectgarbage('collect')
i1:select{}
i2:select{}
i3:select{}

box.error.injection.set('ERRINJ_BUILD_INDEX', i3.id)

i1:alter{parts = {4, "unsigned"}}

_ = collectgarbage('collect')
i1:select{}
i2:select{}
i3:select{}

box.error.injection.set('ERRINJ_BUILD_INDEX', -1)

s:drop()

--
-- Do not rebuild index if the only change is a key part type
-- compatible change.
--
s = box.schema.space.create('test')
pk = s:create_index('pk')
sk = s:create_index('sk', {parts = {2, 'unsigned'}})
s:replace{1, 1}
box.error.injection.set('ERRINJ_BUILD_INDEX', sk.id)
sk:alter({parts = {2, 'number'}})
box.error.injection.set('ERRINJ_BUILD_INDEX', -1)
s:drop()

--
-- gh-3255: iproto can crash and discard responses, if a network
-- is saturated, and DML yields too long on commit.
--

s = box.schema.space.create('test')
_ = s:create_index('pk')
box.schema.user.grant('guest', 'read,write,alter', 'space', 'test')
c = net_box.connect(box.cfg.listen)

ch = fiber.channel(200)
errinj.set("ERRINJ_IPROTO_TX_DELAY", true)
for i = 1, 100 do fiber.create(function() for j = 1, 10 do c.space.test:replace{1} end ch:put(true) end) end
for i = 1, 100 do fiber.create(function() for j = 1, 10 do c.space.test:select() end ch:put(true) end) end
for i = 1, 200 do ch:get() end
errinj.set("ERRINJ_IPROTO_TX_DELAY", false)

s:drop()

--
-- gh-3325: do not cancel already sent requests, when a schema
-- change is detected.
--

box.schema.user.grant('guest', 'execute', 'universe')

s = box.schema.create_space('test')
pk = s:create_index('pk')

box.schema.user.grant('guest', 'read,write,alter', 'space', 'test')
box.schema.user.grant('guest', 'create', 'space')
box.schema.user.grant('guest', 'write', 'space', '_index')
s:replace{1, 1}
cn = net_box.connect(box.cfg.listen)
errinj.set("ERRINJ_WAL_DELAY", true)
ok = nil
err = nil
test_run:cmd('setopt delimiter ";"')
f = fiber.create(function()
  local str = 'box.space.test:create_index("sk", {parts = {{2, "integer"}}})'
  ok, err = pcall(cn.eval, cn, str)
end)
test_run:cmd('setopt delimiter ""');
cn.space.test:get{1}
errinj.set("ERRINJ_WAL_DELAY", false)
while ok == nil do fiber.sleep(0.01) end
ok, err
cn:close()
s:drop()
box.schema.user.revoke('guest', 'execute', 'universe')
box.schema.user.revoke('guest', 'create', 'space')
box.schema.user.revoke('guest', 'write', 'space', '_index')
--
-- If message memory pool is used up, stop the connection, until
-- the pool has free memory.
--
started = 0
finished = 0
continue = false
test_run:cmd('setopt delimiter ";"')
function long_poll_f()
    started = started + 1
    f = fiber.self()
    while not continue do fiber.sleep(0.01) end
    finished = finished + 1
end;

box.schema.func.create('long_poll_f');
box.schema.user.grant('guest', 'execute', 'function', 'long_poll_f');

test_run:cmd('setopt delimiter ""');
cn = net_box.connect(box.cfg.listen)
function long_poll() cn:call('long_poll_f') end
_ = fiber.create(long_poll)
while started ~= 1 do fiber.sleep(0.01) end
-- Simulate OOM for new requests.
errinj.set("ERRINJ_TESTING", true)
-- This request tries to allocate memory for request data and
-- fails. This stops the connection until an existing
-- request is finished.
log = require('log')
-- Fill the log with garbage to not accidentally read log messages
-- produced by a previous test.
log.info(string.rep('a', 1000))
_ = fiber.create(long_poll)
while not test_run:grep_log('default', 'can not allocate memory for a new message', 1000) do fiber.sleep(0.01) end
test_run:grep_log('default', 'stopping input on connection', 1000) ~= nil
started == 1
continue = true
errinj.set("ERRINJ_TESTING", false)
-- Ensure that when memory is available again, the pending
-- request is executed.
while finished ~= 2 do fiber.sleep(0.01) end
cn:close()

box.schema.user.revoke('guest', 'execute', 'function', 'long_poll_f')
box.schema.func.drop('long_poll_f')
--
-- gh-3289: drop/truncate leaves the space in inconsistent
-- state if WAL write fails.
--
s = box.schema.space.create('test')
_ = s:create_index('pk')
for i = 1, 10 do s:replace{i} end
errinj.set('ERRINJ_WAL_IO', true)
s:drop()
s:truncate()
s:drop()
s:truncate()
errinj.set('ERRINJ_WAL_IO', false)
for i = 1, 10 do s:replace{i + 10} end
s:select()
s:drop()

--
-- gh-3432: check that deletion of temporary tuples is not delayed
-- if snapshot is in progress.
--
test_run:cmd("create server test with script='box/lua/cfg_memory.lua'")
test_run:cmd(string.format("start server test with args='%d'", 100 * 1024 * 1024))
test_run:cmd("switch test")

fiber = require('fiber')

-- Create a persistent space.
_ = box.schema.space.create('test')
_ = box.space.test:create_index('pk')
for i = 1, 100 do box.space.test:insert{i} end

-- Create a temporary space.
count = 500
pad = string.rep('x', 100 * 1024)
_ = box.schema.space.create('tmp', {temporary = true})
_ = box.space.tmp:create_index('pk')
for i = 1, count do box.space.tmp:insert{i, pad} end

-- Start background snapshot.
c = fiber.channel(1)
box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', true)
_ = fiber.create(function() box.snapshot() c:put(true) end)

-- Overwrite data stored in the temporary space while snapshot
-- is in progress to make sure that tuples stored in it are freed
-- immediately.
for i = 1, count do box.space.tmp:delete{i} end
_ = collectgarbage('collect')
for i = 1, count do box.space.tmp:insert{i, pad} end

box.error.injection.set('ERRINJ_SNAP_WRITE_DELAY', false)
c:get()

box.space.tmp:drop()
box.space.test:drop()

test_run:cmd("switch default")
test_run:cmd("stop server test")
test_run:cmd("cleanup server test")

--
-- gh-3406: check that incomplete files got cleaned up after restart.
--
fio = require('fio')
fiber = require('fiber')

-- Check that snap.inprogress files are removed.
_ = box.schema.space.create('test')
_ = box.space.test:create_index('primary')
for i = 1, 10 do box.space.test:insert{i} end

errinj.set('ERRINJ_SNAP_WRITE_DELAY', true)
_ = fiber.create(function() box.snapshot() end)
path = fio.pathjoin(box.cfg.memtx_dir, '*.snap.inprogress')
while #fio.glob(path) == 0 do fiber.sleep(0.001) end
#fio.glob(path) > 0

test_run:cmd('restart server default')

fio = require('fio')
fiber = require('fiber')
errinj = box.error.injection

#fio.glob(fio.pathjoin(box.cfg.memtx_dir, "*.snap.inprogress")) == 0
box.space.test:drop()

-- Check that run.inprogress, index.inprogress, and vylog.inprogress
-- files are removed.
_ = box.schema.space.create('test', {engine = 'vinyl'})
_ = box.space.test:create_index('primary')

errinj.set('ERRINJ_VY_LOG_FILE_RENAME', true)
box.snapshot()
errinj.set('ERRINJ_VY_LOG_FILE_RENAME', false)

errinj.set('ERRINJ_VY_GC', true)

errinj.set('ERRINJ_VY_RUN_FILE_RENAME', true)
box.space.test:insert{1}
box.snapshot() -- error
errinj.set('ERRINJ_VY_RUN_FILE_RENAME', false)

-- Wait for the scheduler to unthrottle.
repeat fiber.sleep(0.001) until pcall(box.snapshot)

errinj.set('ERRINJ_VY_INDEX_FILE_RENAME', true)
box.space.test:insert{2}
box.snapshot() -- error
errinj.set('ERRINJ_VY_INDEX_FILE_RENAME', false)

errinj.set('ERRINJ_VY_GC', false)

test_run:cmd('restart server default')

fio = require('fio')
#fio.glob(fio.pathjoin(box.cfg.vinyl_dir, '*.vylog.inprogress')) == 0
#fio.glob(fio.pathjoin(box.cfg.vinyl_dir, box.space.test.id, 0, '*.run.inprogress')) == 0
#fio.glob(fio.pathjoin(box.cfg.vinyl_dir, box.space.test.id, 0, '*.index.inprogress')) == 0

box.space.test:drop()

-- gh-4276 - check grant privilege rollback
_ = box.schema.user.create('testg')
_ = box.schema.space.create('testg'):create_index('pk')

box.error.injection.set('ERRINJ_WAL_IO', true)
-- the grant operation above fails and test hasn't any space test permissions
box.schema.user.grant('testg', 'read,write', 'space', 'testg')
-- switch user and check they couldn't select
box.session.su('testg')
box.space.testg:select()
box.session.su('admin')
box.error.injection.set('ERRINJ_WAL_IO', false)
box.schema.user.drop('testg')
box.space.testg:drop()

--
-- Errinj:get().
--
box.error.injection.get('bad name')
box.error.injection.set('ERRINJ_WAL_IO', true)
box.error.injection.get('ERRINJ_WAL_IO')
box.error.injection.set('ERRINJ_WAL_IO', false)
box.error.injection.get('ERRINJ_WAL_IO')
box.error.injection.set('ERRINJ_TUPLE_FORMAT_COUNT', 20)
box.error.injection.get('ERRINJ_TUPLE_FORMAT_COUNT')
box.error.injection.set('ERRINJ_TUPLE_FORMAT_COUNT', -1)
box.error.injection.get('ERRINJ_TUPLE_FORMAT_COUNT')
box.error.injection.set('ERRINJ_RELAY_TIMEOUT', 0.5)
box.error.injection.get('ERRINJ_RELAY_TIMEOUT')
box.error.injection.set('ERRINJ_RELAY_TIMEOUT', 0)
box.error.injection.get('ERRINJ_RELAY_TIMEOUT')


--
-- gh-4619: make sure that if OOM takes place during rtree recovery,
-- Tarantool instance will fail gracefully.
--
test_run:cmd('create server rtree with script = "box/lua/cfg_rtree.lua"')
test_run:cmd("start server rtree")
test_run:cmd('switch rtree')
math = require("math")
rtreespace = box.schema.create_space('rtree', {if_not_exists = true})
rtreespace:create_index('pk', {if_not_exists = true})
rtreespace:create_index('target', {type='rtree', dimension = 3, parts={2, 'array'},unique = false, if_not_exists = true,})
count = 10
for i = 1, count do box.space.rtree:insert{i, {(i + 1) -\
    math.floor((i + 1)/7000) * 7000, (i + 2) - math.floor((i + 2)/7000) * 7000,\
    (i + 3) - math.floor((i + 3)/7000) * 7000}} end
rtreespace:count()
box.snapshot()
test_run:cmd('switch default')
test_run:cmd("stop server rtree")
test_run:cmd("start server rtree with crash_expected=True")
fio = require('fio')
fh = fio.open(fio.pathjoin(fio.cwd(), 'cfg_rtree.log'), {'O_RDONLY'})
size = fh:seek(0, 'SEEK_END')
fh:seek(-256, 'SEEK_END') ~= nil
line = fh:read(256)
fh:close()
string.match(line, 'Failed to allocate') ~= nil

--
-- gh-5958: make sure that if there is no memory to make
-- possible rollback of replace during shadow building of index
-- creator will fail with appropriate error and replace will happen
--

fiber = require('fiber')

s = box.schema.space.create('test')
_ = s:create_index('pk')
started = false

test_run:cmd("setopt delimiter ';'");

-- Create table with enough tuples to make index build in background.
for i = 1, 150 do
    s:replace{i, i, "valid"}
end;

-- Make fiber joinable.
function joinable(fib)
    fib:set_joinable(true)
    return fib
end;

-- Wait for fiber yield during building of index and then insert new tuple.
function disturb()
    while not started do fiber.sleep(0) end
    s:insert{0, 0, "new"}
end;

-- Build secondary index in background.
-- If the index will not be built in background, test will not be passed
-- because it will run out of time (disturber:join() will wait forever).
function create(is_unique)
    started = true
    s:create_index('sk', {unique=is_unique, type='tree', parts={{2, 'uint'}}})
    started = false
end;

test_run:cmd("setopt delimiter ''");

box.error.injection.set('ERRINJ_BUILD_INDEX_ON_ROLLBACK_ALLOC', true)

disturber = joinable(fiber.new(disturb))
creator = joinable(fiber.new(create, false))

disturber:join()
-- Allocation error is expected in creator
creator:join()

-- Replace must happen
assert(s:get{0} ~= nil)

box.error.injection.set('ERRINJ_BUILD_INDEX_ON_ROLLBACK_ALLOC', false)
started = nil
s:drop()
