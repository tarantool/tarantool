test_run = require('test_run').new()
fio = require('fio')
fiber = require('fiber')
errinj = box.error.injection

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
test_run:grep_log('default', 'get.* took too long')
test_run:grep_log('default', 'select.* took too long')
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
box.error.injection.set('ERRINJ_VY_RUN_WRITE_DELAY', true)
-- Before failing on quota timeout, the following fiber
-- will trigger dump due to memory shortage.
_ = fiber.create(function() s:insert{2, 2, string.rep('x', box.cfg.vinyl_memory)} end)
-- Let the fiber run.
fiber.sleep(0)
-- Drop the space while the dump task is still running.
s:drop()
-- Wait for the dump task to complete.
box.error.injection.set('ERRINJ_VY_RUN_WRITE_DELAY', false)
box.snapshot()
test_run:cmd('switch default')
test_run:cmd("stop server test") -- don't stuck
test_run:cmd("cleanup server test")

--
-- Check that all dump/compaction tasks that are in progress at
-- the time when the server stops are aborted immediately.
--
test_run:cmd("create server double_quota with script='vinyl/low_quota.lua'")
test_run:cmd("start server double_quota with args='2097240'")
test_run:cmd('switch double_quota')
fiber = require 'fiber'
box.cfg{vinyl_timeout = 0.001}
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('i1', {parts = {1, 'unsigned'}})
_ = s:create_index('i2', {parts = {2, 'unsigned'}})
box.error.injection.set('ERRINJ_VY_DUMP_DELAY', true)
for i = 1, 1000 do s:replace{i, i} end
_ = fiber.create(function() box.snapshot() end)
fiber.sleep(0.01)
test_run:cmd('switch default')
test_run:cmd("stop server double_quota") -- don't stuck
test_run:cmd("cleanup server double_quota")

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
_ = fiber.create(do_read)
test_run:wait_cond(function() return sk:stat().disk.iterator.get.rows > 0 end, 60)
pk:stat().disk.iterator.get.rows -- 0
sk:stat().disk.iterator.get.rows -- 1
s:replace{2, 2}
errinj.set("ERRINJ_VY_DELAY_PK_LOOKUP", false)
test_run:wait_cond(function() return pk:stat().get.rows > 0 end, 60)
pk:stat().get.rows -- 1
sk:stat().get.rows -- 1
ret
s:drop()

--
-- gh-3412 - assertion failure at exit in case:
-- * there is a fiber waiting for quota
-- * there is a pending vylog write
--
test_run:cmd("create server low_quota with script='vinyl/low_quota.lua'")
test_run:cmd("start server low_quota with args='1048576'")
test_run:cmd('switch low_quota')
_ = box.schema.space.create('test', {engine = 'vinyl'})
_ = box.space.test:create_index('pk')
box.error.injection.set('ERRINJ_VY_RUN_WRITE_STMT_TIMEOUT', 0.01)
fiber = require('fiber')
pad = string.rep('x', 100 * 1024)
_ = fiber.create(function() for i = 1, 11 do box.space.test:replace{i, pad} end end)
repeat fiber.sleep(0.001) until box.cfg.vinyl_memory - box.stat.vinyl().memory.level0 < pad:len()
test_run:cmd("restart server low_quota with args='1048576'")
box.error.injection.set('ERRINJ_VY_LOG_FLUSH_DELAY', true)
fiber = require('fiber')
pad = string.rep('x', 100 * 1024)
_ = fiber.create(function() for i = 1, 11 do box.space.test:replace{i, pad} end end)
repeat fiber.sleep(0.001) until box.cfg.vinyl_memory - box.stat.vinyl().memory.level0 < pad:len()
test_run:cmd('switch default')
test_run:cmd("stop server low_quota")
test_run:cmd("cleanup server low_quota")

--
-- gh-3437: if compaction races with checkpointing, it may remove
-- files needed for backup.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {run_count_per_level = 1})
-- Create a run file.
_ = s:replace{1}
box.snapshot()
-- Create another run file. This will trigger compaction
-- as run_count_per_level is set to 1. Due to the error
-- injection compaction will finish before snapshot.
_ = s:replace{2}
errinj.set('ERRINJ_SNAP_COMMIT_DELAY', true)
c = fiber.channel(1)
_ = fiber.create(function() box.snapshot() c:put(true) end)
while s.index.pk:stat().disk.compaction.count == 0 do fiber.sleep(0.001) end
errinj.set('ERRINJ_SNAP_COMMIT_DELAY', false)
c:get()
-- Check that all files corresponding to the last checkpoint
-- are present.
files = box.backup.start()
missing = {}
for _, f in pairs(files) do if not fio.path.exists(f) then table.insert(missing, f) end end
missing
box.backup.stop()
s:drop()

--
-- Check that tarantool doesn't hang or crash if error
-- occurs while writing a deferred DELETE to WAL.
--
fiber = require('fiber')
errinj = box.error.injection

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {run_count_per_level = 10})
_ = s:create_index('sk', {unique = false, parts = {2, 'unsigned'}})
s:replace{1, 10}
-- Some padding to prevent last-level compaction (gh-3657).
for i = 1001, 1010 do s:replace{i, i} end
box.snapshot()
s:replace{1, 20}
box.snapshot()

errinj.set("ERRINJ_VY_SCHED_TIMEOUT", 0.001)
errinj.set("ERRINJ_WAL_IO", true)
errors = box.stat.ERROR.total
s.index.pk:compact()
while box.stat.ERROR.total - errors == 0 do fiber.sleep(0.001) end
s.index.pk:stat().disk.compaction.count -- 0
errinj.set("ERRINJ_WAL_IO", false)
while s.index.pk:stat().disk.compaction.count == 0 do fiber.sleep(0.001) end
s.index.pk:stat().disk.compaction.count -- 1
errinj.set("ERRINJ_VY_SCHED_TIMEOUT", 0)

box.snapshot() -- ok
s:drop()

--
-- Check that an instance doesn't crash if a run file needed for
-- joining a replica is corrupted (see gh-3708).
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
s:replace{1, 2, 3}
box.snapshot()
box.schema.user.grant('guest', 'replication')
errinj.set('ERRINJ_VY_READ_PAGE', true)
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica with crash_expected=True")
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")
errinj.set('ERRINJ_VY_READ_PAGE', false)
box.schema.user.revoke('guest', 'replication')
s:drop()

--
-- Check that tarantool stops immediately even if a vinyl worker
-- thread is blocked (see gh-3225).
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
s:replace{1, 1}
box.snapshot()

errinj.set('ERRINJ_VY_READ_PAGE_TIMEOUT', 9000)
_ = fiber.create(function() s:get(1) end)

s:replace{1, 2}

errinj.set('ERRINJ_VY_RUN_WRITE_STMT_TIMEOUT', 9000)
_ = fiber.create(function() box.snapshot() end)

test_run:cmd("restart server default")
box.space.test:drop()

--
-- Check that remote transactions are not aborted when an instance
-- switches to read-only mode (gh-4016).
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
s:replace{1, 1}
box.schema.user.grant('guest', 'replication')
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")

test_run:cmd("switch replica")
box.error.injection.set('ERRINJ_VY_READ_PAGE_TIMEOUT', 0.1)

test_run:cmd("switch default")
s:update({1}, {{'+', 2, 1}})

test_run:cmd("switch replica")
box.cfg{read_only = true}

test_run:cmd("switch default")
vclock = test_run:get_vclock("default")

-- Ignore 0-th vclock component. They don't match between
-- replicas.
vclock[0] = nil
_ = test_run:wait_vclock("replica", vclock)

test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")
box.schema.user.revoke('guest', 'replication')
s:drop()
