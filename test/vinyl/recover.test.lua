fiber = require('fiber')
test_run = require('test_run').new()

-- Temporary table to restore variables after restart.
var = box.schema.space.create('var')
_ = var:create_index('primary', {parts = {1, 'string'}})

-- Empty space.
s1 = box.schema.space.create('test1', {engine = 'vinyl'})
_ = s1:create_index('pk')

-- Truncated space.
s2 = box.schema.space.create('test2', {engine = 'vinyl'})
_ = s2:create_index('pk')
_ = s2:insert{123}
s2:truncate()

-- Data space.
s3 = box.schema.space.create('test3', {engine='vinyl'})
_ = s3:create_index('primary')
_ = s3:create_index('secondary', {unique = false, parts = {2, 'string'}})
for i = 0, 4 do s3:insert{i, 'test' .. i} end

-- Flush data to disk.
box.snapshot()

-- Write some data to memory.
for i = 5, 9 do s3:insert{i, 'test' .. i} end

-- Concurrent index creation (gh-2288).
ch = fiber.channel(2)
s4 = box.schema.space.create('test4', {engine = 'vinyl'})
s5 = box.schema.space.create('test5', {engine = 'vinyl'})
_ = fiber.create(function() s4:create_index('i1') s4:create_index('i2') ch:put(true) end)
_ = fiber.create(function() s5:create_index('i1') s5:create_index('i2') ch:put(true) end)
ch:get()
ch:get()

s4:insert{44}
s5:insert{55}

-- Remember stats before restarting the server.
_ = var:insert{'vyinfo', s3.index.primary:info()}

test_run:cmd('restart server default')

s1 = box.space.test1
s2 = box.space.test2
s3 = box.space.test3
s4 = box.space.test4
s5 = box.space.test5
var = box.space.var

-- Check space contents.
s1:select()
s2:select()
s3.index.primary:select()
s3.index.secondary:select()
s4.index.i1:select()
s4.index.i2:select()
s5.index.i1:select()
s5.index.i2:select()

-- Check that stats didn't change after recovery.
vyinfo1 = var:get('vyinfo')[2]
vyinfo2 = s3.index.primary:info()

vyinfo1.memory.rows == vyinfo2.memory.rows
vyinfo1.memory.bytes == vyinfo2.memory.bytes
vyinfo1.disk.rows == vyinfo2.disk.rows
vyinfo1.disk.bytes == vyinfo2.disk.bytes
vyinfo1.disk.bytes_compressed == vyinfo2.disk.bytes_compressed
vyinfo1.disk.pages == vyinfo2.disk.pages
vyinfo1.run_count == vyinfo2.run_count
vyinfo1.range_count == vyinfo2.range_count

s1:drop()
s2:drop()
s3:drop()
s4:drop()
s5:drop()
var:drop()


test_run:cmd('create server force_recovery with script="vinyl/force_recovery.lua"')
test_run:cmd('start server force_recovery')
test_run:cmd('switch force_recovery')
fio = require'fio'

test = box.schema.space.create('test', {engine = 'vinyl'})
_ = test:create_index('pk')
for i = 0, 9999 do test:replace({i, i, string.rep('a', 512)}) end
box.snapshot()
for i = 10000, 11999 do test:delete({i - 10000}) end
box.snapshot()
for i = 12000, 13999 do test:upsert({i - 10000, i, string.rep('a', 128)}, {{'+', 2, 5}}) end
box.snapshot()
for _, f in pairs(fio.glob(box.cfg.vinyl_dir .. '/' .. test.id .. '/0/*.index')) do fio.unlink(f) end

_ = box.schema.space.create('info')
_ = box.space.info:create_index('pk')
_ = box.space.info:insert{1, box.space.test.index.pk:info()}

test2 = box.schema.space.create('test2', {engine = 'vinyl'})
_ = test2:create_index('pk')
_ = test2:create_index('sec', {parts = {4, 'unsigned', 2, 'string'}})
test2:replace({1, 'a', 2, 3})
test2:replace({2, 'd', 4, 1})
test2:replace({3, 'c', 6, 7})
test2:replace({4, 'b', 6, 3})
box.snapshot()
for _, f in pairs(fio.glob(box.cfg.vinyl_dir .. '/' .. test2.id .. '/0/*.index')) do fio.unlink(f) end
for _, f in pairs(fio.glob(box.cfg.vinyl_dir .. '/' .. test2.id .. '/1/*.index')) do fio.unlink(f) end

test_run = require('test_run').new()
test_run:cmd('switch default')
test_run:cmd('stop server force_recovery')
test_run:cmd('start server force_recovery')
test_run:cmd('switch force_recovery')
sum = 0
for k, v in pairs(box.space.test:select()) do sum = sum + v[2] end
-- should be a sum(2005 .. 4004) + sum(4000 .. 9999) = 48006000
sum

-- Check that disk stats are restored after index rebuild (gh-3173).
old_info = box.space.info:get(1)[2]
new_info = box.space.test.index.pk:info()
new_info.disk.index_size == old_info.disk.index_size
new_info.disk.bloom_size == old_info.disk.bloom_size
new_info.disk.rows == old_info.disk.rows
new_info.disk.bytes == old_info.disk.bytes
new_info.disk.bytes_compressed == old_info.disk.bytes_compressed
new_info.disk.pages == old_info.disk.pages
new_info.run_count == old_info.run_count
new_info.range_count == old_info.range_count

box.space.test2:select()
box.space.test2.index.sec:select()
test_run:cmd('switch default')
test_run:cmd('stop server force_recovery')
test_run:cmd('delete server force_recovery')

-- garbaged vy run indexes
test_run:cmd('create server force_recovery with script="vinyl/bad_run_indexes.lua"')
test_run:cmd('start server force_recovery')
test_run:cmd('switch force_recovery')
test = box.schema.space.create('test', {engine = 'vinyl'})
_ = test:create_index('pk')
for i = 0, 9999 do test:replace({i, i, string.rep('a', 512)}) end
box.snapshot()
for i = 10000, 11999 do test:delete({i - 10000}) end
box.snapshot()
for i = 12000, 13999 do test:upsert({i - 10000, i, string.rep('a', 128)}, {{'+', 2, 5}}) end
box.snapshot()
test_run:cmd('switch default')
test_run:cmd('stop server force_recovery')
test_run:cmd('start server force_recovery')
test_run:cmd('switch force_recovery')
sum = 0
for k, v in pairs(box.space.test:select()) do sum = sum + v[2] end
-- should be a sum(2005 .. 4004) + sum(4000 .. 9999) = 48006000
sum

test_run:cmd('switch default')
test_run:cmd('stop server force_recovery')
test_run:cmd('cleanup server force_recovery')
