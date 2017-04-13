test_run = require('test_run').new()
fiber = require('fiber')
fio = require('fio')

errinj = box.error.injection

-- Temporary space for bumping lsn.
temp = box.schema.space.create('temp')
_ = temp:create_index('pk')

s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk', {run_count_per_level=1})
path = fio.pathjoin(box.cfg.vinyl_dir, tostring(s.id), tostring(s.index.pk.id))

function run_count() return s.index.pk:info().run_count end
function file_count() return #fio.glob(fio.pathjoin(path, '*')) end
function snapshot() box.snapshot() box.internal.gc.run(box.info.signature) end

--
-- Check that gc retries to delete files left
-- from compacted runs.
--

errinj.set('ERRINJ_VY_GC', true)
s:insert{12345, 'abcdef'} snapshot() -- dump
s:insert{67890, 'ghijkl'} snapshot() -- dump + compaction
while run_count() > 1 do fiber.sleep(0.01) end -- wait for compaction
file_count()
temp:auto_increment{} snapshot()
file_count()
errinj.set('ERRINJ_VY_GC', false)
temp:auto_increment{} snapshot()
file_count()

--
-- Check that gc retries to delete files left
-- from dropped indexes.
--

errinj.set('ERRINJ_VY_GC', true)
s:drop() snapshot()
file_count()
errinj.set('ERRINJ_VY_GC', false)
temp:auto_increment{} snapshot()
file_count()

--
-- Check that files left from incomplete runs are deleted
-- upon recovery completion.
--

s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk', {run_count_per_level=1})
path = fio.pathjoin(box.cfg.vinyl_dir, tostring(s.id), tostring(s.index.pk.id))

s:insert{100, '12345'} snapshot() -- dump
file_count()
errinj.set('ERRINJ_VY_RUN_DISCARD', true)
errinj.set('ERRINJ_VY_TASK_COMPLETE', true)
s:insert{200, '67890'} snapshot() -- run file created, but dump fails
file_count()

test_run:cmd('restart server default')

test_run = require('test_run').new()
fio = require('fio')

s = box.space.test
temp = box.space.temp

path = fio.pathjoin(box.cfg.vinyl_dir, tostring(s.id), tostring(s.index.pk.id))

function file_count() return #fio.glob(fio.pathjoin(path, '*')) end
function snapshot() box.snapshot() box.internal.gc.run(box.info.signature) end

file_count()

s:select()

--
-- Cleanup.
--

s:drop() snapshot()
file_count()

temp:drop()
