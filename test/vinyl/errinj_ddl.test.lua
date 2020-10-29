test_run = require('test_run').new()
fiber = require('fiber')
errinj = box.error.injection

--
-- Check that modifications done to the space during the final dump
-- of a newly built index are recovered properly.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')

for i = 1, 5 do s:replace{i, i} end

errinj.set("ERRINJ_VY_RUN_WRITE_DELAY", true)
ch = fiber.channel(1)
_ = fiber.create(function() s:create_index('sk', {parts = {2, 'integer'}}) ch:put(true) end)
test_run:wait_cond(function() return box.stat.vinyl().scheduler.tasks_inprogress > 0 end)

_ = s:delete{1}
_ = s:replace{2, -2}
_ = s:delete{2}
_ = s:replace{3, -3}
_ = s:replace{3, -2}
_ = s:replace{3, -1}
_ = s:delete{3}
_ = s:upsert({3, 3}, {{'=', 2, 1}})
_ = s:upsert({3, 3}, {{'=', 2, 2}})
_ = s:delete{3}
_ = s:replace{4, -1}
_ = s:replace{4, -2}
_ = s:replace{4, -4}
_ = s:upsert({5, 1}, {{'=', 2, 1}})
_ = s:upsert({5, 2}, {{'=', 2, -5}})
_ = s:replace{6, -6}
_ = s:upsert({7, -7}, {{'=', 2, -7}})

errinj.set("ERRINJ_VY_RUN_WRITE_DELAY", false)
ch:get()

s.index.sk:select()
s.index.sk:stat().memory.rows

test_run:cmd('restart server default')

fiber = require('fiber')
errinj = box.error.injection

s = box.space.test

s.index.sk:select()
s.index.sk:stat().memory.rows

box.snapshot()

s.index.sk:select()
s.index.sk:stat().memory.rows

s:drop()

-- exclude_null: correct recovering tuples from memory (vy_build_recover_stmt)
s = box.schema.space.create('test', {engine='vinyl'})
_ = s:create_index('pk')

s:replace{1, 1}
s:replace{2, 2}

errinj.set("ERRINJ_VY_RUN_WRITE_DELAY", true)
ch = fiber.channel(1)
_ = fiber.create(function() s:create_index('sk', {parts = {2, 'integer', exclude_null=true}}) ch:put(true) end)
test_run:wait_cond(function() return box.stat.vinyl().scheduler.tasks_inprogress > 0 end)

_ = s:replace{2, box.NULL}

errinj.set("ERRINJ_VY_RUN_WRITE_DELAY", false)
ch:get()

test_run:cmd('restart server default')

s = box.space.test
s:select{}
s.index.sk:select{}
s:drop()

fiber = require('fiber')
errinj = box.error.injection

--
-- gh-3458: check that rw transactions that started before DDL are
-- aborted.
--
vinyl_cache = box.cfg.vinyl_cache
box.cfg{vinyl_cache = 0}

s1 = box.schema.space.create('test1', {engine = 'vinyl'})
i1 = s1:create_index('pk', {page_size = 16})
s2 = box.schema.space.create('test2', {engine = 'vinyl'})
i2 = s2:create_index('pk')

pad = string.rep('x', 16)
for i = 101, 200 do s1:replace{i, i, pad} end
box.snapshot()

test_run:cmd("setopt delimiter ';'")
function async_replace(space, tuple, wait_cond)
    local c = fiber.channel(1)
    fiber.create(function()
        box.begin()
        space:replace(tuple)
        test_run:wait_cond(wait_cond)
        local status = pcall(box.commit)
        c:put(status)
    end)
    return c
end;
test_run:cmd("setopt delimiter ''");

-- Wait until DDL starts scanning the altered space.
lookup = i1:stat().disk.iterator.lookup
wait_cond = function() return i1:stat().disk.iterator.lookup > lookup end
c1 = async_replace(s1, {1}, wait_cond)
c2 = async_replace(s2, {1}, wait_cond)

errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", 0.001)
s1:format{{'key', 'unsigned'}, {'value', 'unsigned'}}
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", 0)

c1:get() -- false (transaction was aborted)
c2:get() -- true

s1:get(1) == nil
s2:get(1) ~= nil
s1:format()
s1:format{}

-- Wait until DDL starts scanning the altered space.
lookup = i1:stat().disk.iterator.lookup
wait_cond = function() return i1:stat().disk.iterator.lookup > lookup end
c1 = async_replace(s1, {2}, wait_cond)
c2 = async_replace(s2, {2}, wait_cond)

errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", 0.001)
_ = s1:create_index('sk', {parts = {2, 'unsigned'}})
errinj.set("ERRINJ_VY_READ_PAGE_TIMEOUT", 0)

c1:get() -- false (transaction was aborted)
c2:get() -- true

s1:get(2) == nil
s2:get(2) ~= nil
s1.index.pk:count() == s1.index.sk:count()

s1:drop()
s2:drop()
box.cfg{vinyl_cache = vinyl_cache}

-- Transactions that reached WAL must not be aborted.
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')

errinj.set('ERRINJ_WAL_DELAY', true)
_ = fiber.create(function() s:replace{1} end)
_ = fiber.create(function() fiber.sleep(0.01) errinj.set('ERRINJ_WAL_DELAY', false) end)

fiber.sleep(0)
s:format{{'key', 'unsigned'}, {'value', 'unsigned'}} -- must fail
s:select()
s:truncate()

errinj.set('ERRINJ_WAL_DELAY', true)
_ = fiber.create(function() s:replace{1} end)
_ = fiber.create(function() fiber.sleep(0.01) errinj.set('ERRINJ_WAL_DELAY', false) end)

fiber.sleep(0)
s:create_index('sk', {parts = {2, 'unsigned'}})
s:select()
s:drop()

--
-- gh-4000: index iterator crashes if used throughout DDL.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
_ = s:create_index('sk', {parts = {2, 'unsigned'}})

s:replace{1, 1}
box.snapshot()

errinj.set('ERRINJ_VY_READ_PAGE_TIMEOUT', 0.01)
c = fiber.channel(1)
_ = fiber.create(function() c:put(s.index.sk:select()) end)
s.index.sk:alter{parts = {2, 'number'}}
errinj.set('ERRINJ_VY_READ_PAGE_TIMEOUT', 0)

c:get()

s:drop()

--
-- gh-4070: a transaction aborted by DDL must fail any DML/DQL request.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
s:replace{1, 1}

box.begin()
s:replace{1, 2}

ch = fiber.channel(1)
fiber = fiber.create(function() s:create_index('sk', {parts = {2, 'unsigned'}}) ch:put(true) end)
ch:get()

s:get(1)
s:replace{1, 3}
box.commit()

s:drop()

--
-- gh-3420: crash if DML races with DDL.
--
fiber = require('fiber')

-- turn off cache for error injection to work
default_vinyl_cache = box.cfg.vinyl_cache
box.cfg{vinyl_cache = 0}

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('i1')
_ = s:create_index('i2', {parts = {2, 'unsigned'}})
_ = s:create_index('i3', {parts = {3, 'unsigned'}})
_ = s:create_index('i4', {parts = {4, 'unsigned'}})
for i = 1, 5 do s:replace({i, i, i, i}) end
box.snapshot()

test_run:cmd("setopt delimiter ';'")
function async(f)
    local ch = fiber.channel(1)
    fiber.create(function()
        local _, res = pcall(f)
        ch:put(res)
    end)
    return ch
end;
test_run:cmd("setopt delimiter ''");

-- Issue a few DML requests that require reading disk.
-- Stall disk reads.
errinj.set('ERRINJ_VY_READ_PAGE_DELAY', true)

ch1 = async(function() s:insert({1, 1, 1, 1}) end)
ch2 = async(function() s:replace({2, 3, 2, 3}) end)
ch3 = async(function() s:update({3}, {{'+', 4, 1}}) end)
ch4 = async(function() s:upsert({5, 5, 5, 5}, {{'+', 4, 1}}) end)

-- Execute a DDL operation on the space.
s.index.i4:drop()

-- Resume the DML requests. Check that they have been aborted.
errinj.set('ERRINJ_VY_READ_PAGE_DELAY', false)
ch1:get()
ch2:get()
ch3:get()
ch4:get()
s:select()
s:drop()

--
-- gh-4152: yet another DML vs DDL race.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('primary')
s:replace{1, 1}
box.snapshot()

test_run:cmd("setopt delimiter ';'")
-- Create a secondary index. Delay dump.
box.error.injection.set('ERRINJ_VY_DUMP_DELAY', true);
ch = fiber.channel(1);
_ = fiber.create(function()
    local status, err = pcall(s.create_index, s, 'secondary',
                    {unique = true, parts = {2, 'unsigned'}})
    ch:put(status or err)
end);
-- Wait for dump to start.
test_run:wait_cond(function()
    return box.stat.vinyl().scheduler.tasks_inprogress > 0
end);
-- Issue a DML request that will yield to check the unique
-- constraint of the new index. Delay disk read.
box.error.injection.set('ERRINJ_VY_READ_PAGE_DELAY', true);
_ = fiber.create(function()
    local status, err = pcall(s.replace, s, {2, 1})
    ch:put(status or err)
end);
-- Wait for index creation to complete.
-- It must complete successfully.
box.error.injection.set('ERRINJ_VY_DUMP_DELAY', false);
ch:get();
-- Wait for the DML request to complete.
-- It must be aborted.
box.error.injection.set('ERRINJ_VY_READ_PAGE_DELAY', false);
ch:get();
test_run:cmd("setopt delimiter ''");

s.index.primary:select()
s.index.secondary:select()
s:drop()

--
-- gh-4109: crash if space is dropped while space.get is reading from it.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('primary')
s:replace{1}
box.snapshot()

test_run:cmd("setopt delimiter ';'")
box.error.injection.set('ERRINJ_VY_READ_PAGE_TIMEOUT', 0.01);
ch = fiber.channel(1);
_ = fiber.create(function()
    local _, ret = pcall(s.get, s, 1)
    ch:put(ret)
end);
s:drop();
ch:get();
box.error.injection.set('ERRINJ_VY_READ_PAGE_TIMEOUT', 0);
test_run:cmd("setopt delimiter ''");

box.cfg{vinyl_cache = default_vinyl_cache}

--
-- Check that DDL fails if it fails to flush pending WAL writes.
-- Done in the scope of gh-1271.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('primary')
s:replace{1, 2}

box.error.injection.set('ERRINJ_WAL_SYNC', true)

s:format({{'a', 'unsigned'}, {'b', 'unsigned'}}) -- ok
_ = s:create_index('secondary', {parts = {2, 'unsigned'}}) -- ok

s:format({})
s.index.secondary:drop()

box.error.injection.set('ERRINJ_WAL_DELAY', true)
_ = fiber.create(function() s:replace{3, 4} end)

s:format({{'a', 'unsigned'}, {'b', 'unsigned'}}) -- error
_ = s:create_index('secondary', {parts = {2, 'unsigned'}}) -- error

box.error.injection.set('ERRINJ_WAL_DELAY', false)
box.error.injection.set('ERRINJ_WAL_SYNC', false)
s:drop()
