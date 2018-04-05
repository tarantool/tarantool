fiber = require('fiber')
test_run = require('test_run').new()

-- sanity checks
space = box.schema.space.create('test', {engine = 'vinyl' })
space:create_index('pk', {range_size = 0})
space:create_index('pk', {page_size = 0})
space:create_index('pk', {page_size = 8192, range_size = 4096})
space:create_index('pk', {run_count_per_level = 0})
space:create_index('pk', {run_size_ratio = 1})
space:create_index('pk', {bloom_fpr = 0})
space:create_index('pk', {bloom_fpr = 1.1})
space:drop()

-- space secondary index create
space = box.schema.space.create('test', { engine = 'vinyl' })
index1 = space:create_index('primary')
index2 = space:create_index('secondary')
space:drop()

-- space index create hash
space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary', {type = 'hash'})
space:drop()

-- creation of a new index and altering the definition of an existing
-- index are unsupported for non-empty spaces
space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert({1})
-- fail because of wrong tuple format {1}, but need {1, ...}
index2 = space:create_index('secondary', { parts = {2, 'unsigned'} })
space.index.primary:alter({parts = {1, 'unsigned', 2, 'unsigned'}})
#box.space._index:select({space.id})
box.space._index:get{space.id, 0}[6]
space:drop()

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert({1, 2})
index2 = space:create_index('secondary', { parts = {2, 'unsigned'} })
space.index.primary:alter({parts = {1, 'unsigned', 2, 'unsigned'}})
#box.space._index:select({space.id})
box.space._index:get{space.id, 0}[6]
space:drop()

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert({1, 2})
index2 = space:create_index('secondary', { parts = {2, 'unsigned'} })
space.index.primary:alter({parts = {1, 'unsigned', 2, 'unsigned'}})
#box.space._index:select({space.id})
box.space._index:get{space.id, 0}[6]
space:delete({1})

-- must fail because vy_mems have data
index2 = space:create_index('secondary', { parts = {2, 'unsigned'} })
space.index.primary:alter({parts = {1, 'unsigned', 2, 'unsigned'}})
box.snapshot()
while space.index.primary:info().rows ~= 0 do fiber.sleep(0.01) end

-- after a dump REPLACE + DELETE = nothing, so the space is empty now and
-- can be altered.
index2 = space:create_index('secondary', { parts = {2, 'unsigned'} })
space.index.primary:alter({parts = {1, 'unsigned', 2, 'unsigned'}})
#box.space._index:select({space.id})
box.space._index:get{space.id, 0}[6]
space:insert({1, 2})
index:select{}
index2:select{}
space:drop()

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary', { run_count_per_level = 2 })
space:insert({1, 2})
box.snapshot()
space:delete({1})
box.snapshot()
while space.index.primary:info().run_count ~= 2 do fiber.sleep(0.01) end
-- must fail because vy_runs have data
index2 = space:create_index('secondary', { parts = {2, 'unsigned'} })
space.index.primary:alter({parts = {1, 'unsigned', 2, 'unsigned'}})

-- After compaction the REPLACE + DELETE + DELETE = nothing, so
-- the space is now empty and can be altered.
space:delete({1})
-- Make sure the run is big enough to trigger compaction.
space:replace({2, 3})
space:delete({2})
box.snapshot()
-- Wait until the dump is finished.
while space.index.primary:info().rows ~= 0 do fiber.sleep(0.01) end
index2 = space:create_index('secondary', { parts = {2, 'unsigned'} })
space.index.primary:alter({parts = {1, 'unsigned', 2, 'unsigned'}})

space:drop()

--
-- gh-1709: need error on altering space
--
space = box.schema.space.create('test', {engine='vinyl'})
pk = space:create_index('pk', {parts = {1, 'unsigned'}})
space:auto_increment{1}
space:auto_increment{2}
space:auto_increment{3}
box.space._index:replace{space.id, 0, 'pk', 'tree', {unique=true}, {{0, 'unsigned'}, {1, 'unsigned'}}}
space:select{}
space:drop()

-- Allow to specify various bloom fprs per index.
space = box.schema.space.create('test', {engine='vinyl'})
pk = space:create_index('pk', {bloom_fpr = 0.1})
sec = space:create_index('sec', {bloom_fpr = 0.2})
third = space:create_index('third', {bloom_fpr = 0.3})
pk.options.bloom_fpr
sec.options.bloom_fpr
third.options.bloom_fpr
space:drop()

--
-- gh-2109: allow alter some opts of not empty indexes
--
-- Forst, check that we can decrease run_count_per_level and it
-- triggers compaction after next box.snapshot(). Ensure that the
-- runs with different page_sizes and bloom_fprs are compacted
-- correctly.
--
space = box.schema.space.create('test', {engine='vinyl'})
page_size = 8192
range_size = 1024 * 1024 * 1024
bloom_fpr = 0.1
pk = space:create_index('pk', {run_count_per_level = 10, page_size = page_size, range_size = range_size, bloom_fpr = bloom_fpr})
pad_size = page_size / 5
pad = string.rep('I', pad_size)
-- Create 4 pages with sizes 'page_size'
for i = 1, 20 do space:replace{i, pad} end
est_bsize = pad_size * 20
box.snapshot()
pk:info().disk.pages
space.index.pk.options.page_size
pk:info().run_count
space.index.pk.options.bloom_fpr

-- Change page_size and trigger compaction
page_size = page_size * 2
bloom_fpr = bloom_fpr * 2
pk:alter({page_size = page_size, run_count_per_level = 1, bloom_fpr = bloom_fpr})
pad_size = page_size / 5
pad = string.rep('I', pad_size)
-- Create 4 pages with new sizes in new run
for i = 1, 20 do space:replace{i + 20, pad} end
est_bsize = est_bsize + pad_size * 20
box.snapshot()
-- Wait for compaction
while pk:info().run_count ~= 1 do fiber.sleep(0.01) end
pk:info().disk.pages
space.index.pk.options.page_size
pk:info().run_count
space.index.pk.options.bloom_fpr
est_bsize / page_size == pk:info().disk.pages
space:drop()

--
-- Change range size to trigger split.
--
space = box.schema.space.create('test', {engine = 'vinyl'})
page_size = 64
range_size = page_size * 15
pk = space:create_index('pk', {page_size = page_size, range_size = range_size, run_count_per_level = 1})
pad = ''
for i = 1, 64 do pad = pad..(i % 10) end
for i = 1, 8 do space:replace{i, pad} end
box.snapshot()

-- Decrease the range_size and dump many runs to trigger split.
pk:alter({range_size = page_size * 2})
while pk:info().range_count < 2 do space:replace{1, pad} box.snapshot() fiber.sleep(0.01) end

space:drop()

-- gh-2673 vinyl cursor uses already freed VinylIndex and vy_index
s = box.schema.space.create('test', {engine = 'vinyl'})
i0 = s:create_index('i0', {parts = {1, 'string'}})
i1 = s:create_index('i1', {unique = false, parts = {2, 'string', 3, 'string', 4, 'string'}})
i2 = s:create_index('i2', {parts = {2, 'string', 4, 'string', 3, 'string', 1, 'string'}})
i3 = s:create_index('i3', {parts = {2, 'string', 4, 'string', 6, 'unsigned', 1, 'string'}})

test_run:cmd("setopt delimiter ';'")
for j = 1, 60 do
    s:truncate()
    self = {}
    self.end2018 = os.time{year=2018, month=12, day=31, hour=23, min=59, sec=59}
    self.start2019 = os.time{year=2019, month=1, day=1, hour=0, min=0, sec=0}
    self.week1end = os.time{year=2019, month=1, day=6, hour=23, min=59, sec=59}
    self.week2start = os.time{year=2019, month=1, day=7, hour=0, min=0, sec=0}
    local iface1 = s:insert{'id1', 'uid1', 'iid1', 'fid1', {1, 2, 3, 4}, self.end2018}
    local iface2 = s:insert{'id2', 'uid1', 'iid1', 'fid1', {1, 2, 3, 4}, self.start2019}
    local iface3 = s:insert{'id3', 'uid1', 'iid1', 'fid1', {1, 2, 3, 4}, self.week1end}
    local iface4 = s:insert{'id4', 'uid1', 'iid1', 'fid1', {1, 2, 3, 4}, self.week2start}
    local f, ctx, state = s.index.i3:pairs({'uid1', 'fid1', 0x7FFFFFFF}, { iterator='LE' })
    state, tup = f(ctx, state)
    state, tup = f(ctx, state)
end ;
test_run:cmd("setopt delimiter ''");

s:drop()

-- gh-2342 cursors after death of index
create_iterator = require('utils').create_iterator
s = box.schema.space.create('test', { engine = 'vinyl' })
pk = s:create_index('primary', { parts = { 1, 'uint' } })
sk = s:create_index('sec', { parts = { 2, 'uint' } })
s:replace{1, 2, 3}
s:replace{4, 5, 6}
s:replace{7, 8, 9}
itr = create_iterator(s, {})
f, ctx, state = s.index.sec:pairs({5}, { iterator='LE' })
itr.next()
f(ctx, state)
s:drop()
itr.next()
f(ctx, state)
f = nil
ctx = nil
state = nil
itr = nil
collectgarbage('collect')

-- gh-2342 drop space if transaction is in progress
ch = fiber.channel(1)
s = box.schema.space.create('test', { engine = 'vinyl' })
pk = s:create_index('primary', { parts = { 1, 'uint' } })
sk = s:create_index('sec', { parts = { 2, 'uint' } })
box.begin()
s:replace({1, 2, 3})
s:replace({4, 5, 6})
s:replace({7, 8, 9})
s:upsert({10, 11, 12}, {})
_ = fiber.create(function () s:drop() ch:put(true) end)
ch:get()
box.commit()

s = box.schema.space.create('test', { engine = 'vinyl' })
pk = s:create_index('primary', { parts = { 1, 'uint' } })
sk = s:create_index('sec', { parts = { 2, 'uint' } })
box.begin()
s:replace{1, 2, 3}
s:replace{4, 5, 6}
s:replace{7, 8, 9}
_ = fiber.create(function () s:drop() ch:put(true) end)
ch:get()
box.commit()

-- check invalid field types
space = box.schema.space.create('test', { engine = 'vinyl' })
pk = space:create_index('primary')
index = space:create_index('test', { type = 'tree', parts = { 2, 'nosuchtype' }})
index = space:create_index('test', { type = 'tree', parts = { 2, 'any' }})
index = space:create_index('test', { type = 'tree', parts = { 2, 'array' }})
index = space:create_index('test', { type = 'tree', parts = { 2, 'map' }})
space:drop()

-- gh-3019 default index options
box.space._space:insert{512, 1, 'test', 'vinyl', 0, setmetatable({}, {__serialize = 'map'}), {}}
box.space._index:insert{512, 0, 'pk', 'tree', {unique = true}, {{0, 'unsigned'}}}
box.space.test.index.pk
box.space.test:drop()

-- gh-2449 change 'unique' index property from true to false
s = box.schema.space.create('test', { engine = 'vinyl' })
_ = s:create_index('primary')
_ = s:create_index('secondary', {unique = true, parts = {2, 'unsigned'}})
s:insert{1, 10}
s.index.secondary:alter{unique = false} -- ok
s.index.secondary.unique
s.index.secondary:alter{unique = true} -- error
s.index.secondary.unique
s:insert{2, 10}
s.index.secondary:select(10)
s:drop()

-- Narrowing indexed field type entails index rebuild.
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('i1')
_ = s:create_index('i2', {parts = {2, 'integer'}})
_ = s:create_index('i3', {parts = {{3, 'string', is_nullable = true}}})
_ = s:replace{1, 1, 'test'}
-- Should fail with 'Vinyl does not support building an index for a non-empty space'.
s.index.i2:alter{parts = {2, 'unsigned'}}
s.index.i3:alter{parts = {{3, 'string', is_nullable = false}}}
s:drop()
