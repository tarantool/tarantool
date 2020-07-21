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
        squashed = stat2.upsert.squashed - stat1.upsert.squashed,
        applied = stat2.upsert.applied - stat1.upsert.applied
    }
end;
test_run:cmd("setopt delimiter ''");

space = box.schema.space.create('test', { engine = 'vinyl' })
index = space:create_index('primary')

stat1 = index:stat()

-- separate upserts w/o on disk data
space:upsert({1, 1, 1}, {{'+', 2, 10}})
space:upsert({1, 1, 1}, {{'-', 2, 20}})
space:upsert({1, 1, 1}, {{'=', 2, 20}})

stat2 = index:stat()
upsert_stat_diff(stat2, stat1)
stat1 = stat2

stat1.rows

-- in-tx upserts
box.begin()
space:upsert({2, 1, 1}, {{'+', 2, 10}})
space:upsert({2, 1, 1}, {{'-', 2, 20}})
space:upsert({2, 1, 1}, {{'=', 2, 20}})
box.commit()

stat2 = index:stat()
upsert_stat_diff(stat2, stat1)
stat1 = stat2

stat1.rows

box.snapshot()

index:stat().rows

-- upsert with on disk data
space:upsert({1, 1, 1}, {{'+', 2, 10}})
space:upsert({1, 1, 1}, {{'-', 2, 20}})

stat2 = index:stat()
upsert_stat_diff(stat2, stat1)
stat1 = stat2

stat1.rows

-- count of applied apserts
space:get({1})
stat2 = index:stat()
upsert_stat_diff(stat2, stat1)
stat1 = stat2

space:get({2})
stat2 = index:stat()
upsert_stat_diff(stat2, stat1)
stat1 = stat2

space:select({})
stat2 = index:stat()
upsert_stat_diff(stat2, stat1)
stat1 = stat2

-- start upsert optimizer
for i = 0, 999 do space:upsert({3, 0, 0}, {{'+', 2, 1}}) end
stat2 = index:stat()
upsert_stat_diff(stat2, stat1)
stat1 = stat2
space:get{3}

stat1.rows

space:drop()

-- fix behaviour after https://github.com/tarantool/tarantool/issues/2104
s = box.schema.space.create('test', {engine = 'vinyl'})
i = s:create_index('test', { run_count_per_level = 20 })

-- Write a big run to prevent last-level compaction (gh-3657).
for i = 101, 110 do s:replace{i, require('digest').urandom(50)} end

s:replace({1, 1})
box.snapshot()
s:upsert({1, 1}, {{'+', 1, 1}}) -- ignored due to primary key changed
s:upsert({1, 1}, {{'+', 2, 1}}) -- applied to the previous statement
s:get(1)

--
-- gh-2520 use cache as a hint when applying upserts.
--
old_stat = s.index.test:stat()
-- insert the first upsert
s:upsert({100}, {{'=', 2, 200}})
-- force a dump, the inserted upsert is now on disk
box.snapshot()
-- populate the cache
s:get{100}
-- a lookup in a run was done to populate the cache
new_stat = s.index.test:stat()
upsert_stat_diff(new_stat, old_stat)
new_stat.disk.iterator.lookup - old_stat.disk.iterator.lookup
old_stat = new_stat
-- Add another upsert: the cached REPLACE will be used and the upsert will
-- be applied immediately
s:upsert({100}, {{'=', 2, 300}})
-- force a new dump
box.snapshot()
-- lookup the key
s:get{100}
--
-- since we converted upsert to replace on insert, we had to
-- go no further than the latest dump to locate the latest
-- value of the key
--
new_stat = s.index.test:stat()
upsert_stat_diff(new_stat, old_stat)
new_stat.disk.iterator.lookup - old_stat.disk.iterator.lookup

--
-- gh-3003: crash in read iterator if upsert exactly matches
-- the search key.
--
s:truncate()
s:insert{100, 100}
box.snapshot()
s:upsert({100}, {{'+', 2, 100}})
s:select({100}, 'GE')

s:drop()

--
-- Check that read iterator ignores a cached statement in case
-- the current key is updated while it yields on disk read.
--
fiber = require('fiber')
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
s:replace{10, 10}
box.snapshot()
s:get(10) -- add [10, 10] to the cache
ch = fiber.channel(1)
test_run:cmd("setopt delimiter ';'")
_ = fiber.create(function() ch:put(s:select()) end)
s:upsert({10, 10}, {{'+', 2, 10}})
test_run:cmd("setopt delimiter ''");
ch:get() -- should see the UPSERT and return [10, 20]
s:drop()

-- gh-5106: upsert squash doesn't handle arithmetic operation
-- applied on the set operation.
--
s = box.schema.space.create('test', { engine = 'vinyl'})
_ = s:create_index('pk')
s:replace{1, 1}
box.snapshot()

s:upsert({1, 0}, {{'=', 2, 2}})
s:upsert({1, 0}, {{'-', 2, 1}})
box.snapshot()
s:select()

for i = 0, 11 do if i%2 == 0 then s:upsert({1, 0}, {{'=', 2, i}}) else s:upsert({1, 0}, {{'+', 2, i}}) end end
box.snapshot()
s:select()

-- Operations won't squash (owing to incompatible types), so
-- during applying resulting upsert on the top of replace
-- statement we will get 'double update' error and ignored
-- second upsert.
--
s:upsert({1, 0}, {{'=', 2, 'abc'}})
s:upsert({1, 0}, {{'-', 2, 1}})
box.snapshot()
s:select()

s:drop()

-- gh-5107: don't squash upsert operations into one array.
--
-- gh-5087: test upsert execution/squash referring to fields in reversed
-- order (via negative indexing).
--
s = box.schema.create_space('test', {engine = 'vinyl'})
pk = s:create_index('pk')
s:insert({1, 1, 1})
box.snapshot()

s:upsert({1}, {{'=', 3, 100}})
s:upsert({1}, {{'=', -1, 200}})
box.snapshot()
s:select() -- {1, 1, 200}

s:delete({1})
s:insert({1, 1, 1})
box.snapshot()

s:upsert({1}, {{'=', -3, 100}})
s:upsert({1}, {{'=', -1, 200}})
box.snapshot()
-- gh-5105: Two upserts are NOT squashed into one, so only one (first one)
-- is skipped, meanwhile second one is applied.
--
s:select() -- {1, 1, 1}

s:delete({1})
box.snapshot()

s:upsert({1, 1}, {{'=', -2, 300}}) -- {1, 1}
s:upsert({1}, {{'+', -1, 100}}) -- {1, 101}
s:upsert({1}, {{'-', 2, 100}}) -- {1, 1}
s:upsert({1}, {{'+', -1, 200}}) -- {1, 201}
s:upsert({1}, {{'-', 2, 200}}) -- {1, 1}
box.snapshot()
s:select() -- {1, 1}

s:delete({1})
box.snapshot()

s:upsert({1, 1, 1}, {{'!', -1, 300}}) -- {1, 1, 1}
s:upsert({1}, {{'+', -2, 100}}) -- {1, 101, 1}
s:upsert({1}, {{'=', -1, 100}}) -- {1, 101, 100}
s:upsert({1}, {{'+', -1, 200}}) -- {1, 101, 300}
s:upsert({1}, {{'-', -2, 100}}) -- {1, 1, 300}
box.snapshot()
s:select()

s:drop()

-- gh-1622: upsert operations which break space format are not applied.
--
s = box.schema.space.create('test', { engine = 'vinyl', field_count = 2 })
pk = s:create_index('pk')
s:replace{1, 1}
-- Error is logged, upsert is not applied.
--
s:upsert({1, 1}, {{'=', 3, 5}})
-- During read the incorrect upsert is ignored.
--
s:select{}

-- Try to set incorrect field_count in a transaction.
--
box.begin()
s:replace{2, 2}
s:upsert({2, 2}, {{'=', 3, 2}})
s:select{}
box.commit()
s:select{}

-- Read incorrect upsert from a run: it should be ignored.
--
box.snapshot()
s:select{}
s:upsert({2, 2}, {{'=', 3, 20}})
box.snapshot()
s:select{}

-- Execute replace/delete after invalid upsert.
--
box.snapshot()
s:upsert({2, 2}, {{'=', 3, 30}})
s:replace{2, 3}
s:select{}

s:upsert({1, 1}, {{'=', 3, 30}})
s:delete{1}
s:select{}

-- Invalid upsert in a sequence of upserts is skipped meanwhile
-- the rest are applied.
--
box.snapshot()
s:upsert({2, 2}, {{'+', 2, 5}})
s:upsert({2, 2}, {{'=', 3, 40}})
s:upsert({2, 2}, {{'+', 2, 5}})
s:select{}
box.snapshot()
s:select{}

s:drop()

-- Test different scenarious during which update operations squash can't
-- take place due to format violations.
--
decimal = require('decimal')

s = box.schema.space.create('test', { engine = 'vinyl', field_count = 5 })
s:format({{name='id', type='unsigned'}, {name='u', type='unsigned'},\
          {name='s', type='scalar'}, {name='f', type='double'},\
          {name='d', type='decimal'}})
pk = s:create_index('pk')
s:replace{1, 1, 1, 1.1, decimal.new(1.1) }
s:replace{2, 1, 1, 1.1, decimal.new(1.1)}
box.snapshot()
-- Can't assign integer to float field. First operation is still applied.
--
s:upsert({1, 1, 1, 2.5, decimal.new(1.1)}, {{'+', 4, 4}})
s:upsert({1, 1, 1, 2.5, decimal.new(1.1)}, {{'=', 4, 4}})
-- Can't add floating point to integer (result is floating point).
--
s:upsert({2, 1, 1, 2.5, decimal.new(1.1)}, {{'+', 2, 5}})
s:upsert({2, 1, 1, 2.5, decimal.new(1.1)}, {{'+', 2, 5.5}})
box.snapshot()
s:select()
-- Integer overflow check.
--
s:upsert({1, 1, 1, 2.5, decimal.new(1.1)}, {{'+', 3, 9223372036854775808}})
s:upsert({1, 1, 1, 2.5, decimal.new(1.1)}, {{'+', 3, 9223372036854775808}})
-- Negative result of subtraction stored in unsigned field.
--
s:upsert({2, 1, 1, 2.5, decimal.new(1.1)}, {{'+', 2, 2}})
s:upsert({2, 1, 1, 2.5, decimal.new(1.1)}, {{'-', 2, 10}})
box.snapshot()
s:select()
-- Decimals do not fit into numerics and vice versa.
--
s:upsert({1, 1, 1, 2.5, decimal.new(1.1)}, {{'+', 5, 2}})
s:upsert({1, 1, 1, 2.5, decimal.new(1.1)}, {{'-', 5, 1}})
s:upsert({2, 1, 1, 2.5, decimal.new(1.1)}, {{'+', 2, decimal.new(2.1)}})
s:upsert({2, 1, 1, 2.5, decimal.new(1.1)}, {{'-', 2, decimal.new(1.2)}})
box.snapshot()
s:select()

s:drop()

-- Upserts leading to overflow are ignored.
--
format = {}
format[1] = {name = 'f1', type = 'unsigned'}
format[2] = {name = 'f2', type = 'unsigned'}
s = box.schema.space.create('test', {engine = 'vinyl', format = format})
_ = s:create_index('pk')
uint_max = 18446744073709551615ULL
s:replace{1, uint_max - 2}
box.snapshot()

s:upsert({1, 0}, {{'+', 2, 1}})
s:upsert({1, 0}, {{'+', 2, 1}})
s:upsert({1, 0}, {{'+', 2, 1}})
box.snapshot()
s:select()

s:delete{1}
s:replace{1, uint_max - 2, 0}
box.snapshot()

s:upsert({1, 0, 0}, {{'+', 2, 1}})
s:upsert({1, 0, 0}, {{'+', 2, 1}})
s:upsert({1, 0, 0}, {{'+', 2, 0.5}})
s:upsert({1, 0, 0}, {{'+', 2, 1}})
box.snapshot()
s:select()
s:drop()

-- Make sure upserts satisfy associativity rule.
--
s = box.schema.space.create('test', {engine='vinyl'})
i = s:create_index('pk', {parts={2, 'uint'}})
s:replace{1, 2, 3, 'default'}
box.snapshot()

s:upsert({2, 2, 2}, {{'=', 4, 'upserted'}})
-- Upsert will fail and thus ignored.
--
s:upsert({2, 2, 2}, {{'#', 1, 1}, {'!', 3, 1}})
box.snapshot()

s:select{}

s:drop()

-- Combination of upserts and underlying void (i.e. delete or null)
-- statement on disk. Upsert modifying PK is skipped.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
i = s:create_index('test', { run_count_per_level = 20 })

for i = 101, 110 do s:replace{i, i} end
s:replace({1, 1})
box.snapshot()
s:delete({1})
box.snapshot()
s:upsert({1, 1}, {{'=', 2, 2}})
s:upsert({1, 1}, {{'=', 1, 0}})
box.snapshot()
s:select()

s:drop()

s = box.schema.space.create('test', {engine = 'vinyl'})
i = s:create_index('test', { run_count_per_level = 20 })

for i = 101, 110 do s:replace{i, i} end
box.snapshot()
s:upsert({1, 1}, {{'=', 2, 2}})
s:upsert({1, 1}, {{'=', 1, 0}})
box.snapshot()
s:select()

s:drop()
