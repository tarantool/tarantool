test_run = require('test_run').new()

--
-- Setting bloom_fpr to 1 disables bloom filter.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {bloom_fpr = 1})
for i = 1, 10, 2 do s:insert{i} end
box.snapshot()
for i = 1, 10 do s:get{i} end
stat = s.index.pk:stat()
stat.disk.bloom_size -- 0
stat.disk.iterator.bloom.hit -- 0
stat.disk.iterator.bloom.miss -- 0
s:drop()

-- Disable tuple cache to check bloom hit/miss ratio.
box.cfg{vinyl_cache = 0}

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {parts = {1, 'unsigned', 2, 'unsigned', 3, 'unsigned', 4, 'unsigned'}})

reflects = 0
function cur_reflects() return box.space.test.index.pk:stat().disk.iterator.bloom.hit end
function new_reflects() local o = reflects reflects = cur_reflects() return reflects - o end
seeks = 0
function cur_seeks() return box.space.test.index.pk:stat().disk.iterator.lookup end
function new_seeks() local o = seeks seeks = cur_seeks() return seeks - o end

for i = 1, 1000 do s:replace{math.ceil(i / 10), math.ceil(i / 2), i, i * 2} end
box.snapshot()

--
-- There are 1000 unique tuples in the index. The cardinality of the
-- first key part is 100, of the first two key parts is 500, of the
-- first three key parts is 1000. With the default bloom fpr of 0.05,
-- we use 5 hash functions or 5 / ln(2) ~= 7.3 bits per tuple.If we
-- allocated a full sized bloom filter per each sub key, we would need
-- to allocate at least (100 + 500 + 1000 + 1000) * 7 bits or 2275
-- bytes. However, since we adjust the fpr of bloom filters of higher
-- ranks (because a full key lookup checks all its sub keys as well),
-- we use 5, 4, 3, and 1 hash functions for each sub key respectively.
-- This leaves us only (100*5 + 500*4 + 1000*3 + 1000*1) / ln(2) bits
-- or 1172 bytes, and after rounding up to the block size (128 byte)
-- we have 1280 bytes plus the header overhead.
--
s.index.pk:stat().disk.bloom_size

_ = new_reflects()
_ = new_seeks()

for i = 1, 100 do s:select{i} end
new_reflects() == 0
new_seeks() == 100

for i = 1, 1000 do s:select{math.ceil(i / 10), math.ceil(i / 2)} end
new_reflects() == 0
new_seeks() == 1000

for i = 1, 1000 do s:select{math.ceil(i / 10), math.ceil(i / 2), i} end
new_reflects() == 0
new_seeks() == 1000

for i = 1, 1000 do s:select{math.ceil(i / 10), math.ceil(i / 2), i, i * 2} end
new_reflects() == 0
new_seeks() == 1000

for i = 1001, 2000 do s:select{i} end
new_reflects() > 980
new_seeks() < 20

for i = 1, 1000 do s:select{i, i} end
new_reflects() > 980
new_seeks() < 20

for i = 1, 1000 do s:select{i, i, i} end
new_reflects() > 980
new_seeks() < 20

for i = 1, 1000 do s:select{i, i, i, i} end
new_reflects() > 980
new_seeks() < 20

test_run:cmd('restart server default')

vinyl_cache = box.cfg.vinyl_cache
box.cfg{vinyl_cache = 0}

s = box.space.test

reflects = 0
function cur_reflects() return box.space.test.index.pk:stat().disk.iterator.bloom.hit end
function new_reflects() local o = reflects reflects = cur_reflects() return reflects - o end
seeks = 0
function cur_seeks() return box.space.test.index.pk:stat().disk.iterator.lookup end
function new_seeks() local o = seeks seeks = cur_seeks() return seeks - o end

_ = new_reflects()
_ = new_seeks()

for i = 1, 100 do s:select{i} end
new_reflects() == 0
new_seeks() == 100

for i = 1, 1000 do s:select{math.ceil(i / 10), math.ceil(i / 2)} end
new_reflects() == 0
new_seeks() == 1000

for i = 1, 1000 do s:select{math.ceil(i / 10), math.ceil(i / 2), i} end
new_reflects() == 0
new_seeks() == 1000

for i = 1, 1000 do s:select{math.ceil(i / 10), math.ceil(i / 2), i, i * 2} end
new_reflects() == 0
new_seeks() == 1000

for i = 1001, 2000 do s:select{i} end
new_reflects() > 980
new_seeks() < 20

for i = 1, 1000 do s:select{i, i} end
new_reflects() > 980
new_seeks() < 20

for i = 1, 1000 do s:select{i, i, i} end
new_reflects() > 980
new_seeks() < 20

for i = 1, 1000 do s:select{i, i, i, i} end
new_reflects() > 980
new_seeks() < 20

s:drop()

box.cfg{vinyl_cache = vinyl_cache}
