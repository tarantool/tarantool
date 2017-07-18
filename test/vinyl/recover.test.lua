test_run = require('test_run').new()

-- Temporary table to restore variables after restart.
tmp = box.schema.space.create('tmp')
_ = tmp:create_index('primary', {parts = {1, 'string'}})

--
-- Check that vinyl stats are restored correctly.
--
s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('primary', {run_count_per_level=10})

-- Generate data.
for i=1,2 do for j=1,10 do s:insert{i*100+j, 'test' .. j} end box.snapshot() end

-- Remember stats before restarting the server.
_ = tmp:insert{'vyinfo', s.index.primary:info()}

test_run:cmd('restart server default')

s = box.space.test
tmp = box.space.tmp

-- Check that stats didn't change after recovery.
vyinfo1 = tmp:get('vyinfo')[2]
vyinfo2 = s.index.primary:info()

vyinfo1.memory.rows == vyinfo2.memory.rows
vyinfo1.memory.bytes == vyinfo2.memory.bytes
vyinfo1.disk.rows == vyinfo2.disk.rows
vyinfo1.disk.bytes == vyinfo2.disk.bytes
vyinfo1.disk.bytes_compressed == vyinfo2.disk.bytes_compressed
vyinfo1.disk.pages == vyinfo2.disk.pages
vyinfo1.run_count == vyinfo2.run_count
vyinfo1.range_count == vyinfo2.range_count

s:drop()

tmp:drop()
