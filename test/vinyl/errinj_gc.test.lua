test_run = require('test_run').new()
fiber = require('fiber')
fio = require('fio')

errinj = box.error.injection

-- Temporary space for bumping lsn.
temp = box.schema.space.create('temp')
_ = temp:create_index('pk')

path = test_run:get_cfg('index_options').path
s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk', {path=path, run_count_per_level=1})
if not path then path = fio.pathjoin(box.cfg.vinyl_dir, tostring(s.id), tostring(s.index.pk.id)) end

function run_count() return box.info.vinyl().db[s.id..'/'..s.index.pk.id].run_count end
function file_count() return #fio.glob(fio.pathjoin(path, '*')) end

--
-- Check that on snapshot we retry to delete files left
-- from compacted runs.
--

errinj.set('ERRINJ_VY_GC', true)
s:insert{12345, 'abcdef'} box.snapshot() -- dump
s:insert{67890, 'ghijkl'} box.snapshot() -- dump + compaction
while run_count() > 1 do fiber.sleep(0.01) end -- wait for compaction
file_count()
temp:auto_increment{} box.snapshot()
file_count()
errinj.set('ERRINJ_VY_GC', false)
temp:auto_increment{} box.snapshot()
file_count()

--
-- Check that on snapshot we retry to delete files left
-- from dropped indexes.
--

errinj.set('ERRINJ_VY_GC', true)
s:drop() box.snapshot()
file_count()
errinj.set('ERRINJ_VY_GC', false)
temp:auto_increment{} box.snapshot()
file_count()

--
-- Check that files left from incomplete runs are deleted
-- upon recovery completion.
--

path = test_run:get_cfg('index_options').path
s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk', {path=path, run_count_per_level=1})
if not path then path = fio.pathjoin(box.cfg.vinyl_dir, tostring(s.id), tostring(s.index.pk.id)) end

s:insert{100, '12345'} box.snapshot() -- dump
file_count()
errinj.set('ERRINJ_VY_RUN_DISCARD', true)
errinj.set('ERRINJ_VY_TASK_COMPLETE', true)
s:insert{200, '67890'} box.snapshot() -- run file created, but dump fails
file_count()

test_run:cmd('restart server default')

test_run = require('test_run').new()
fio = require('fio')

s = box.space.test
temp = box.space.temp

path = test_run:get_cfg('index_options').path
if not path then path = fio.pathjoin(box.cfg.vinyl_dir, tostring(s.id), tostring(s.index.pk.id)) end

function file_count() return #fio.glob(fio.pathjoin(path, '*')) end

file_count()

s:select()

--
-- Cleanup.
--

s:drop() box.snapshot()
file_count()

temp:drop()
