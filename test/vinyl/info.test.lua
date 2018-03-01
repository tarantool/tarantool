test_run = require('test_run').new()

-- Since we store LSNs in data files, the data size may differ
-- from run to run. Deploy a new server to make sure it will be
-- the same so that we can check it.
test_run:cmd('create server test with script = "vinyl/info.lua"')
test_run:cmd('start server test')
test_run:cmd('switch test')

fiber = require('fiber')

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {page_size = 4096, range_size = 16384, run_count_per_level = 1, run_size_ratio = 1000})

--
-- Helper functions.
--

test_run:cmd("setopt delimiter ';'")

-- Generate random 1K padding.
function pad()
    local t = {}
    for i = 1, 1024 do
        t[i] = string.char(math.random(65, 90))
    end
    return table.concat(t)
end;

-- Insert a tuple into the test space.
function put(val)
    box.space.test:replace{val, pad()}
end;

-- Compute the difference between two tables containing stats.
-- If a field value is the same, it will be set to nil in the
-- resulting table. If 'path' is not 'nil', compare statistics
-- starting from 'path'.
function stat_diff(stat1, stat2, path)
    while path ~= nil and path ~= '' do
        local i = path:find('%.') or path:len() + 1
        local node = path:sub(1, i - 1)
        path = path:sub(i + 1, path:len())
        stat1 = stat1[node]
        stat2 = stat2[node]
    end
    if type(stat1) == 'string' then
        return nil
    end
    if type(stat1) == 'number' then
        return stat1 ~= stat2 and stat1 - stat2 or nil
    end
    assert(type(stat1) == 'table')
    local diff
    for k, v1 in pairs(stat1) do
        local v2 = stat2[k]
        local d = stat_diff(v1, v2)
        if d ~= nil then
            if diff == nil then
                diff = {}
            end
            diff[k] = d
        end
    end
    return diff
end;

-- Return index statistics.
--
-- Note, latency measurement is beyond the scope of this test
-- so we just filter it out.
function istat()
    local st = box.space.test.index.pk:info()
    st.latency = nil
    return st
end;

-- Return global statistics.
--
-- Note, quota watermark checking is beyond the scope of this
-- test so we just filter out related statistics.
function gstat()
    local st = box.info.vinyl()
    st.quota.use_rate = nil
    st.quota.dump_bandwidth = nil
    st.quota.watermark = nil
    return st
end;

-- Wait until a stat counter changes.
function wait(stat_func, stat, path, diff)
    while (stat_diff(stat_func(), stat, path) or 0) < diff do
        fiber.sleep(0.01)
    end
end;

test_run:cmd("setopt delimiter ''");

-- initially stats are empty
istat()
gstat()

--
-- Index statistics.
--

-- Compressed data size may differ as padding is random.
-- Besides, it may depend on the zstd version so let's
-- filter it out.
test_run:cmd("push filter 'bytes_compressed: .*' to 'bytes_compressed: <bytes_compressed>'")

-- put + dump
st = istat()
for i = 1, 100, 4 do put(i) end
box.snapshot()
wait(istat, st, 'disk.dump.count', 1)
stat_diff(istat(), st)

-- put + dump + compaction
st = istat()
for i = 1, 100, 2 do put(i) end
box.snapshot()
wait(istat, st, 'disk.compact.count', 1)
stat_diff(istat(), st)

-- point lookup from disk + cache put
st = istat()
s:get(1) ~= nil
stat_diff(istat(), st)

-- point lookup from cache
st = istat()
s:get(1) ~= nil
stat_diff(istat(), st)

-- put in memory + cache invalidate
st = istat()
put(1)
stat_diff(istat(), st)

-- point lookup from memory
st = istat()
s:get(1) ~= nil
stat_diff(istat(), st)

-- put in txw + point lookup from txw
st = istat()
box.begin()
put(1)
s:get(1) ~= nil
stat_diff(istat(), st)
box.rollback()

-- apply upsert in txw
st = istat()
box.begin()
_ = s:replace{1}
_ = s:upsert({1}, {{'=', 2, pad()}})
stat_diff(istat(), st, 'upsert')
box.rollback()

-- apply upsert on get
st = istat()
_ = s:upsert({5}, {{'=', 2, pad()}})
s:get(5) ~= nil
stat_diff(istat(), st, 'upsert')

-- cache eviction
assert(box.cfg.vinyl_cache < 100 * 1024)
for i = 1, 100 do put(i) end
st = istat()
for i = 1, 100 do s:get(i) end
stat_diff(istat(), st, 'cache')

-- range split
for i = 1, 100 do put(i) end
st = istat()
box.snapshot()
wait(istat, st, 'disk.compact.count', 2)
st = istat()
st.range_count -- 2
st.run_count -- 2
st.run_avg -- 1
st.run_histogram -- [1]:2

-- range lookup
for i = 1, 100 do put(i) end
box.begin()
for i = 1, 100, 2 do put(i) end
st = istat()
#s:select()
stat_diff(istat(), st)
box.rollback()

-- range lookup from cache
assert(box.cfg.vinyl_cache > 10 * 1024)
for i = 1, 100 do put(i) end
box.begin()
#s:select({}, {limit = 5})
st = istat()
#s:select({}, {limit = 5})
stat_diff(istat(), st)
box.rollback()

--
-- Global statistics.
--

-- use quota
st = gstat()
put(1)
stat_diff(gstat(), st, 'quota')

-- use cache
st = gstat()
_ = s:get(1)
stat_diff(gstat(), st, 'cache')

s:delete(1)

-- rollback
st = gstat()
box.begin()
_ = s:insert{1}
box.rollback()
stat_diff(gstat(), st, 'tx')

-- conflict
st = gstat()
ch1 = fiber.channel(1)
ch2 = fiber.channel(1)
test_run:cmd("setopt delimiter ';'")
_ = fiber.create(function()
    box.begin()
    s:insert{1}
    ch1:put(true)
    ch2:get()
    pcall(box.commit)
    ch1:put(true)
end);
test_run:cmd("setopt delimiter ''");
ch1:get()
_ = s:insert{1}
ch2:put(true)
ch1:get()
stat_diff(gstat(), st, 'tx')

s:delete(1)

-- tx statements
st = gstat()
box.begin()
for i = 1, 10 do s:replace{i} end
stat_diff(gstat(), st, 'tx')
box.rollback()
stat_diff(gstat(), st, 'tx')

-- transactions
st = gstat()
ch1 = fiber.channel(5)
ch2 = fiber.channel(5)
test_run:cmd("setopt delimiter ';'")
for i = 1, 5 do
    fiber.create(function()
        box.begin()
        s:replace{i}
        ch1:put(true)
        ch2:get()
        box.rollback()
        ch1:put(true)
    end)
end;
test_run:cmd("setopt delimiter ''");
for i = 1, 5 do ch1:get() end
stat_diff(gstat(), st, 'tx')
for i = 1, 5 do ch2:put(true) end
for i = 1, 5 do ch1:get() end
stat_diff(gstat(), st, 'tx')

-- read view
st = gstat()
ch1 = fiber.channel(1)
ch2 = fiber.channel(1)
test_run:cmd("setopt delimiter ';'")
_ = fiber.create(function()
    box.begin()
    s:select()
    ch1:put(true)
    ch2:get()
    pcall(box.commit)
    ch1:put(true)
end);
test_run:cmd("setopt delimiter ''");
ch1:get()
_ = s:insert{1}
stat_diff(gstat(), st, 'tx')
ch2:put(true)
ch1:get()
stat_diff(gstat(), st, 'tx')

s:delete(1)

-- gap locks
st = gstat()
box.begin()
_ = s:select({10}, {iterator = 'LT'})
_ = s:select({20}, {iterator = 'GT'})
stat_diff(gstat(), st, 'tx')
box.commit()
stat_diff(gstat(), st, 'tx')

-- box.stat.reset
box.stat.reset()
istat()
gstat()

s:drop()

--
-- space.bsize, index.len, index.bsize
--

s = box.schema.space.create('test', {engine = 'vinyl'})
s:bsize()
i1 = s:create_index('i1', {parts = {1, 'unsigned'}, run_count_per_level = 1})
i2 = s:create_index('i2', {parts = {2, 'unsigned'}, run_count_per_level = 1})
s:bsize()
i1:len(), i2:len()
i1:bsize(), i2:bsize()

for i = 1, 100, 2 do s:replace{i, i, pad()} end
st1 = i1:info()
st2 = i2:info()
s:bsize()
i1:len(), i2:len()
i1:bsize(), i2:bsize()
s:bsize() == st1.memory.bytes
i1:len() == st1.memory.rows
i2:len() == st2.memory.rows
i1:bsize() == st1.memory.index_size
i2:bsize() == st2.memory.index_size

box.snapshot()
st1 = i1:info()
st2 = i2:info()
s:bsize()
i1:len(), i2:len()
i1:bsize(), i2:bsize()
s:bsize() == st1.disk.bytes
i1:len() == st1.disk.rows
i2:len() == st2.disk.rows
i1:bsize() == st1.disk.index_size + st1.disk.bloom_size
i2:bsize() == st2.disk.index_size + st2.disk.bloom_size + st2.disk.bytes

for i = 1, 100, 2 do s:delete(i) end
for i = 2, 100, 2 do s:replace{i, i, pad()} end
st1 = i1:info()
st2 = i2:info()
s:bsize()
i1:len(), i2:len()
i1:bsize(), i2:bsize()
s:bsize() == st1.memory.bytes + st1.disk.bytes
i1:len() == st1.memory.rows + st1.disk.rows
i2:len() == st2.memory.rows + st2.disk.rows
i1:bsize() == st1.memory.index_size + st1.disk.index_size + st1.disk.bloom_size
i2:bsize() == st2.memory.index_size + st2.disk.index_size + st2.disk.bloom_size + st2.disk.bytes

box.snapshot()
wait(function() return i1:info() end, st1, 'disk.compact.count', 1)
wait(function() return i2:info() end, st2, 'disk.compact.count', 1)
st1 = i1:info()
st2 = i2:info()
s:bsize()
i1:len(), i2:len()
i1:bsize(), i2:bsize()
s:bsize() == st1.disk.bytes
i1:len() == st1.disk.rows
i2:len() == st2.disk.rows
i1:bsize() == st1.disk.index_size + st1.disk.bloom_size
i2:bsize() == st2.disk.index_size + st2.disk.bloom_size + st2.disk.bytes

s:drop()

test_run:cmd('switch default')
test_run:cmd('stop server test')
test_run:cmd('cleanup server test')
test_run:cmd("clear filter")
