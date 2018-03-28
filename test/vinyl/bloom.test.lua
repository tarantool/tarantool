test_run = require('test_run').new()

--
-- Setting bloom_fpr to 1 disables bloom filter.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {bloom_fpr = 1})
for i = 1, 10, 2 do s:insert{i} end
box.snapshot()
for i = 1, 10 do s:get{i} end
stat = s.index.pk:info()
stat.disk.bloom_size -- 0
stat.disk.iterator.bloom.hit -- 0
stat.disk.iterator.bloom.miss -- 0
s:drop()

-- Disable tuple cache to check bloom hit/miss ratio.
box.cfg{vinyl_cache = 0}

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {parts = {1, 'unsigned', 2, 'unsigned', 3, 'unsigned', 4, 'unsigned'}})

reflects = 0
function cur_reflects() return box.space.test.index.pk:info().disk.iterator.bloom.hit end
function new_reflects() local o = reflects reflects = cur_reflects() return reflects - o end
seeks = 0
function cur_seeks() return box.space.test.index.pk:info().disk.iterator.lookup end
function new_seeks() local o = seeks seeks = cur_seeks() return seeks - o end

for i = 1, 1000 do s:replace{math.ceil(i / 10), math.ceil(i / 2), i, i * 2} end
box.snapshot()
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
function cur_reflects() return box.space.test.index.pk:info().disk.iterator.bloom.hit end
function new_reflects() local o = reflects reflects = cur_reflects() return reflects - o end
seeks = 0
function cur_seeks() return box.space.test.index.pk:info().disk.iterator.lookup end
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
