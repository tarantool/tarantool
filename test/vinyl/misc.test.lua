test_run = require('test_run').new()
fiber = require('fiber')

--
-- gh-2784: do not validate space formatted but not indexed fields
-- in surrogate statements.
--

-- At first, test simple surrogate delete generated from a key.
format = {{name = 'a', type = 'unsigned'}, {name = 'b', type = 'unsigned'}}
s = box.schema.space.create('test', {engine = 'vinyl', format = format})
_ = s:create_index('pk')
s:insert{1, 1}
-- Type of a second field in a surrogate tuple must be NULL but
-- with UNSIGNED type, specified in a tuple_format. It is
-- possible, because only indexed fields are used in surrogate
-- tuples.
s:delete(1)
s:drop()

-- Test select after snapshot. This select gets surrogate
-- tuples from a disk. Here NULL also can be stored in formatted,
-- but not indexed field.
format = {}
format[1] = {name = 'a', type = 'unsigned'}
format[2] = {name = 'b', type = 'unsigned'}
format[3] = {name = 'c', type = 'unsigned'}
s = box.schema.space.create('test', {engine = 'vinyl', format = format})
_ = s:create_index('pk')
_ = s:create_index('sk', {parts = {2, 'unsigned'}})
s:insert{1, 1, 1}
box.snapshot()
s:delete(1)
box.snapshot()
s:select()
s:drop()

--
-- gh-2983: ensure the transaction associated with a fiber
-- is automatically rolled back if the fiber stops.
--
s = box.schema.create_space('test', { engine = 'vinyl' })
_ = s:create_index('pk')
tx1 = box.stat.vinyl().tx
ch = fiber.channel(1)
_ = fiber.create(function() box.begin() s:insert{1} ch:put(true) end)
ch:get()
tx2 = box.stat.vinyl().tx
tx2.commit - tx1.commit -- 0
tx2.rollback - tx1.rollback -- 1
s:drop()

--
-- gh-3154: check of duplicates is skipped if the index
-- is contained by another unique index which is checked.
--
s = box.schema.create_space('test', {engine = 'vinyl'})
i1 = s:create_index('i1', {unique = true, parts = {1, 'unsigned', 2, 'unsigned'}})
i2 = s:create_index('i2', {unique = true, parts = {2, 'unsigned', 1, 'unsigned'}})
i3 = s:create_index('i3', {unique = true, parts = {3, 'unsigned', 4, 'unsigned', 5, 'unsigned'}})
i4 = s:create_index('i4', {unique = true, parts = {5, 'unsigned', 4, 'unsigned'}})
i5 = s:create_index('i5', {unique = true, parts = {4, 'unsigned', 5, 'unsigned', 1, 'unsigned'}})
i6 = s:create_index('i6', {unique = true, parts = {4, 'unsigned', 6, 'unsigned', 5, 'unsigned'}})
i7 = s:create_index('i7', {unique = true, parts = {6, 'unsigned'}})

-- space.create_index() does a lookup in the primary index
-- so reset the stats before calling space.insert().
box.stat.reset()

s:insert{1, 1, 1, 1, 1, 1}

i1:stat().lookup -- 1
i2:stat().lookup -- 0
i3:stat().lookup -- 0
i4:stat().lookup -- 1
i5:stat().lookup -- 0
i6:stat().lookup -- 0
i7:stat().lookup -- 1

s:drop()

--
-- gh-3643: unique optimization results in skipping uniqueness
-- check in the primary index on insertion.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {unique = true, parts = {1, 'unsigned', 2, 'unsigned'}})
_ = s:create_index('sk', {unique = true, parts = {2, 'unsigned'}})
s:insert{1, 1, 1}
s:insert{1, 1, 2} -- error
s:drop()

--
-- gh-3944: automatic range size configuration.
--
-- Passing range_size explicitly on index creation.
s = box.schema.space.create('test', {engine = 'vinyl'})
i = s:create_index('pk', {range_size = 0})
i.options.range_size -- nil
i:stat().range_size -- 16 MB
box.space._index:get{s.id, i.id}[5].range_size -- 0
s:drop()
-- Inheriting global settings.
test_run:cmd('create server test with script = "vinyl/stat.lua"')
test_run:cmd('start server test')
test_run:cmd('switch test')
box.cfg.vinyl_range_size -- nil
s = box.schema.space.create('test', {engine = 'vinyl'})
i = s:create_index('pk')
i.options.range_size -- nil
i:stat().range_size -- 16 MB
box.space._index:get{s.id, i.id}[5].range_size -- nil
s:drop()
test_run:cmd('switch default')
test_run:cmd('stop server test')
test_run:cmd('cleanup server test')

--
-- gh-4016: local rw transactions are aborted when the instance
-- switches to read-only mode.
--
-- gh-4070: an aborted transaction must fail any DML/DQL request.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
s:replace({1, 1})
test_run:cmd("setopt delimiter ';'")
-- Start rw transaction.
ch1 = fiber.channel(1);
_ = fiber.create(function()
    box.begin()
    s:replace{1, 2}
    ch1:get()
    local status, err = pcall(s.replace, s, {3, 4})
    ch1:put(status or err)
    local status, err = pcall(s.select, s)
    ch1:put(status or err)
    local status, err = pcall(box.commit)
    ch1:put(status or err)
end);
-- Start ro transaction.
ch2 = fiber.channel(1);
_ = fiber.create(function()
    box.begin()
    s:select()
    ch2:get()
    local status, err = pcall(s.select, s)
    ch2:put(status or err)
    local status, err = pcall(box.commit)
    ch2:put(status or err)
end);
test_run:cmd("setopt delimiter ''");
-- Switch to ro mode.
box.cfg{read_only = true}
-- Resume the transactions.
ch1:put(true)
ch2:put(true)
ch1:get()
ch1:get()
ch1:get()
ch2:get()
ch2:get()
-- Cleanup.
box.cfg{read_only = false}
s:select()
s:drop()

--
-- gh-2389: L1 runs are not compressed.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
i = s:create_index('pk', {page_size = 100 * 1000, range_size = 1000 * 1000})
pad = string.rep('x', 1000)
for k = 1, 10 do s:replace{k, pad} end
box.snapshot()
stat = i:stat().disk
stat.bytes_compressed >= stat.bytes
for k = 1, 10 do s:replace{k, pad} end
box.snapshot()
i:compact()
test_run:wait_cond(function() return i:stat().disk.compaction.count > 0 end)
stat = i:stat().disk
stat.bytes_compressed < stat.bytes / 10
s:drop()

-- Vinyl doesn't support functional index.
s = box.schema.space.create('withdata', {engine = 'vinyl'})
lua_code = [[function(tuple) return tuple[1] + tuple[2] end]]
box.schema.func.create('s', {body = lua_code, is_deterministic = true, is_sandboxed = true})
_ = s:create_index('pk')
_ = s:create_index('idx', {func = box.func.s.id, parts = {{1, 'unsigned'}}})
s:drop()
box.schema.func.drop('s')
