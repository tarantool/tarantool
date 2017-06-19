test_run = require('test_run').new()

-- gh-1671 upsert is broken in a transaction

-- upsert after upsert

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert({1, 1, 2})
space:insert({2})
space:insert({3, 4, 'abc'})
box.begin()
space:upsert({1}, {{'#', 3, 1}})
space:upsert({1}, {{'!', 2, 20}})
space:upsert({1}, {{'+', 3, 20}})
box.commit()
space:select{}

box.begin()
space:upsert({2}, {{'!', 2, 10}})
space:upsert({3, 4, 5}, {{'+', 2, 1}})
space:upsert({2, 2, 2, 2}, {{'+', 2, 10.5}})
space:upsert({3}, {{'-', 2, 2}})
box.commit()
space:select{}

space:drop()

-- upsert after replace

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert{1}
space:insert{2}

box.begin()
space:replace({3, 4})
space:upsert({3, 3, 3, 3}, {{'+', 2, 1}})
box.commit()
space:select{}

box.begin()
space:replace({2, 2})
space:upsert({2}, {{'!', 2, 1}})
space:upsert({2}, {{'!', 2, 3}})
box.commit()
space:select{}

box.begin()
space:replace({4})
space:upsert({4}, {{'!', 2, 1}})
space:replace({5})
space:upsert({4}, {{'!', 2, 3}})
space:upsert({5}, {{'!', 2, 1}, {'+', 2, 1}})
box.commit()
space:select{}

space:drop()

-- upsert after delete

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert{1}
space:insert{2}
space:insert{3}
space:insert{4}

box.begin()
space:delete({1})
space:upsert({1, 2}, {{'!', 2, 100}})
box.commit()
space:select{}

box.begin()
space:delete({2})
space:upsert({1}, {{'+', 2, 1}})
space:upsert({2, 200}, {{'!', 2, 1000}})
space:upsert({2}, {{'!', 2, 1005}})
box.commit()
space:select{}

space:drop()

-- replace after upsert

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert{1}
space:insert{2}
space:insert{3}
space:insert{4}

box.begin()
space:upsert({1, 2}, {{'!', 2, 100}})
space:replace({1, 2, 3})
box.commit()
space:select{}

box.begin()
space:upsert({2}, {{'!', 2, 2}})
space:upsert({3}, {{'!', 2, 3}})
space:replace({2, 20})
space:replace({3, 30})
box.commit()
space:select{}

space:drop()

-- delete after upsert

box.cfg{}
space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert{1}
space:insert{2}
space:insert{3}
space:insert{4}

box.begin()
space:upsert({1, 2}, {{'!', 2, 100}})
space:delete({1})
box.commit()
space:select{}

box.begin()
space:upsert({5}, {{'!', 2, 100}})
space:delete({5})
box.commit()
space:select{}

box.begin()
space:upsert({5}, {{'!', 2, 100}})
space:delete({4})
space:upsert({4}, {{'!', 2, 100}})
space:delete({5})
space:upsert({4}, {{'!', 2, 105}})
box.commit()
space:select{}


space:drop()

--
-- gh-1829: vinyl: merge hot UPSERTS in the background
-- gh-1828: Automatically convert UPSERT into REPLACE
-- gh-1826: vinyl: memory explosion on UPSERT
--

clock = require 'clock'

space = box.schema.space.create('test', { engine = 'vinyl' })
_ = space:create_index('primary', { type = 'tree', range_size = 250 * 1024 * 1024 } )

test_run:cmd("setopt delimiter ';'")
-- add a lot of UPSERT statements to the space
function gen()
    for i=1,2000 do space:upsert({0, 0}, {{'+', 2, 1}}) end
end;
-- check that 'get' takes reasonable time
function check()
    local start = clock.monotonic()
    for i=1,1000 do space:get(0) end
    return clock.monotonic() - start < 1
end;
test_run:cmd("setopt delimiter ''");

-- No runs
gen()
check() -- exploded before #1826

-- Mem has DELETE
box.snapshot()
space:delete({0})
gen()
check() -- exploded before #1826

-- Mem has REPLACE
box.snapshot()
space:replace({0, 0})
gen()
check() -- exploded before #1826

-- Mem has only UPSERTS
box.snapshot()
gen()
check() -- exploded before #1829

space:drop()


-- test upsert statistic against some upsert scenarous

test_run:cmd("setopt delimiter ';'")
function upsert_stat_diff(stat2, stat1)
    return {
        squashed = stat2.upsert_squashed.total - stat1.upsert_squashed.total,
        applied = stat2.upsert_applied.total - stat1.upsert_applied.total
    }
end;
test_run:cmd("setopt delimiter ''");

stat1 = box.info.vinyl().performance

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')

-- separate upserts w/o on disk data
space:upsert({1, 1, 1}, {{'+', 2, 10}})
space:upsert({1, 1, 1}, {{'-', 2, 20}})
space:upsert({1, 1, 1}, {{'=', 2, 20}})

stat2 = box.info.vinyl().performance
upsert_stat_diff(stat2, stat1)
stat1 = stat2

space.index.primary:info().rows

-- in-tx upserts
box.begin()
space:upsert({2, 1, 1}, {{'+', 2, 10}})
space:upsert({2, 1, 1}, {{'-', 2, 20}})
space:upsert({2, 1, 1}, {{'=', 2, 20}})
box.commit()

stat2 = box.info.vinyl().performance
upsert_stat_diff(stat2, stat1)
stat1 = stat2

space.index.primary:info().rows

box.snapshot()

space.index.primary:info().rows

-- upsert with on disk data
space:upsert({1, 1, 1}, {{'+', 2, 10}})
space:upsert({1, 1, 1}, {{'-', 2, 20}})

stat2 = box.info.vinyl().performance
upsert_stat_diff(stat2, stat1)
stat1 = stat2

space.index.primary:info().rows

-- count of applied apserts
space:get({1})
stat2 = box.info.vinyl().performance
upsert_stat_diff(stat2, stat1)
stat1 = stat2

space:get({2})
stat2 = box.info.vinyl().performance
upsert_stat_diff(stat2, stat1)
stat1 = stat2

space:select({})
stat2 = box.info.vinyl().performance
upsert_stat_diff(stat2, stat1)
stat1 = stat2

-- start upsert optimizer
for i = 0, 999 do space:upsert({3, 0, 0}, {{'+', 2, 1}}) end
stat2 = box.info.vinyl().performance
upsert_stat_diff(stat2, stat1)
stat1 = stat2
space:get{3}

space.index.primary:info().rows

space:drop()

-- fix behaviour after https://github.com/tarantool/tarantool/issues/2104
s = box.schema.space.create('test', {engine = 'vinyl'})
i = s:create_index('test', { run_count_per_level = 20 })

s:replace({1, 1})
box.snapshot()
s:upsert({1, 1}, {{'+', 1, 1}})
s:upsert({1, 1}, {{'+', 2, 1}})
s:select() --both upserts are ignored due to primary key change

-- gh-2520 use cache as a hint for upserts applying.
old_stat = box.info.vinyl().performance["iterator"].run.lookup_count
s:upsert({100}, {{'=', 2, 200}})
box.snapshot()
s:get{100}
new_stat = box.info.vinyl().performance["iterator"].run.lookup_count
new_stat - old_stat
old_stat = new_stat
box.snapshot()
s:upsert({100}, {{'=', 2, 300}})
box.snapshot()
s:get{100}
new_stat = box.info.vinyl().performance["iterator"].run.lookup_count
new_stat - old_stat
old_stat = new_stat

s:drop()
