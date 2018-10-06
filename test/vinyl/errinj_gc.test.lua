test_run = require('test_run').new()
fiber = require('fiber')
fio = require('fio')

errinj = box.error.injection

test_run:cleanup_cluster()

-- Make each snapshot trigger garbage collection.
box.cfg{checkpoint_count = 1}

-- Temporary space for bumping lsn.
temp = box.schema.space.create('temp')
_ = temp:create_index('pk')

s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk', {run_count_per_level=1})
path = fio.pathjoin(box.cfg.vinyl_dir, tostring(s.id), tostring(s.index.pk.id))

function file_count() return #fio.glob(fio.pathjoin(path, '*')) end
function gc() temp:auto_increment{} box.snapshot() end

--
-- Check that gc retries to delete files left
-- from compacted runs.
--

errinj.set('ERRINJ_VY_GC', true)
s:insert{12345, 'abcdef'} box.snapshot() -- dump
s:insert{67890, 'ghijkl'} box.snapshot() -- dump + compaction
while s.index.pk:stat().run_count > 1 do fiber.sleep(0.01) end -- wait for compaction
file_count()
gc()
file_count()
errinj.set('ERRINJ_VY_GC', false)
gc()
file_count()

--
-- Check that gc retries to delete files left
-- from dropped indexes.
--

errinj.set('ERRINJ_VY_GC', true)
s:drop()
gc()
file_count()
errinj.set('ERRINJ_VY_GC', false)
gc()
file_count()

--
-- Check that files left from incomplete runs are deleted
-- upon recovery completion.
--

s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk', {run_count_per_level=1})
path = fio.pathjoin(box.cfg.vinyl_dir, tostring(s.id), tostring(s.index.pk.id))

s:insert{100, '12345'} box.snapshot() -- dump
file_count()
errinj.set('ERRINJ_VY_RUN_DISCARD', true)
errinj.set('ERRINJ_VY_TASK_COMPLETE', true)
s:insert{200, '67890'} box.snapshot() -- run file created, but dump fails
file_count()

test_run:cmd('restart server default')

test_run = require('test_run').new()
fio = require('fio')

default_checkpoint_count = box.cfg.checkpoint_count
box.cfg{checkpoint_count = 1}

s = box.space.test
temp = box.space.temp

path = fio.pathjoin(box.cfg.vinyl_dir, tostring(s.id), tostring(s.index.pk.id))

function file_count() return #fio.glob(fio.pathjoin(path, '*')) end
function gc() temp:auto_increment{} box.snapshot() end

file_count()

s:select()

s:drop()
gc()
file_count()

temp:drop()

box.cfg{checkpoint_count = default_checkpoint_count}

--
-- Check that if we failed to clean up an incomplete index before restart,
-- we will clean it up after recovery.
--
fio = require('fio')
fiber = require('fiber')

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
_ = s:insert{1, 1}

box.error.injection.set('ERRINJ_WAL_DELAY', true)
ch = fiber.channel(1)
_ = fiber.create(function() pcall(s.create_index, s, 'sk', {parts = {2, 'unsigned'}}) ch:put(true) end)

-- wait for ALTER to stall on WAL after preparing the new index
while s.index.pk:stat().disk.dump.count == 0 do fiber.sleep(0.001) end

box.error.injection.set('ERRINJ_VY_GC', true)
box.error.injection.set('ERRINJ_VY_LOG_FLUSH', true);
box.error.injection.set("ERRINJ_WAL_WRITE", true)
box.error.injection.set('ERRINJ_WAL_DELAY', false)
ch:get()

#fio.listdir(fio.pathjoin(box.cfg.vinyl_dir, s.id, 1)) > 0

test_run:cmd('restart server default')

fio = require('fio')
s = box.space.test
#fio.listdir(fio.pathjoin(box.cfg.vinyl_dir, s.id, 1)) == 0
s:drop()
