test_run = require('test_run').new()
fiber = require('fiber')
fio = require('fio')

index_options = test_run:get_cfg('index_options')
index_options.run_count_per_level = 2

s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk', index_options)

path = index_options.path
if not path then path = fio.pathjoin(box.cfg.vinyl_dir, tostring(s.id), tostring(s.index.pk.id)) end

function run_count() return box.info.vinyl().db[s.id..'/'..s.index.pk.id].run_count end
function file_count() return #fio.glob(fio.pathjoin(path, '*')) end

-- Check that run files are normally removed right after compaction.
s:insert{1} box.snapshot() -- dump
s:insert{2} box.snapshot() -- dump
file_count()
s:insert{3} box.snapshot() -- dump + compaction
while run_count() > 1 do fiber.sleep(0.01) end -- wait for compaction
file_count()

-- Check that files left from dropped indexes are deleted on snapshot.
s:drop() box.snapshot()
file_count()
