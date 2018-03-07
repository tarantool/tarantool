#!/usr/bin/env tarantool

test_run = require('test_run').new()

test_run:cmd("setopt delimiter ';'")
stat = nil
function stat_changed()
    local old_stat = stat
    local new_stat = box.space.test.index.pk:info()
    stat = new_stat
    return (old_stat == nil or
            old_stat.memory.iterator.lookup ~= new_stat.memory.iterator.lookup or
            old_stat.memory.iterator.get.rows ~= new_stat.memory.iterator.get.rows or
            old_stat.disk.iterator.lookup ~= new_stat.disk.iterator.lookup or
            old_stat.disk.iterator.get.rows ~= new_stat.disk.iterator.get.rows)
end;
test_run:cmd("setopt delimiter ''");

s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk')

str = string.rep('!', 100)

for i = 1,1000 do s:insert{i, str} end

box.begin()
t = s:select{}
box.commit()

#t

t = s:replace{100, str}

for i = 1,10 do box.begin() t = s:select{} box.commit() end

t = s:replace{200, str}

s:drop()

s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {parts = {1, 'uint', 2, 'uint'}})

str = ''

s:replace{0, 0, 0}
s:replace{1, 1, 1, str}
s:replace{1, 2, 1, str}
s:replace{1, 3, 1, str}
s:replace{1, 4, 1, str}
s:replace{2, 1, 2, str}
s:replace{2, 2, 2, str}
s:replace{2, 3, 2, str}
s:replace{2, 4, 2, str}
s:replace{3, 3, 4}

box.snapshot()
_ = stat_changed() -- init

box.begin()
s:get{1, 2}
box.commit()
stat_changed()  -- cache miss, true

s:get{1, 2}
stat_changed() -- cache hit, false

box.begin()
s:select{1}
box.commit()
stat_changed()  -- cache miss, true

s:select{1}
stat_changed() -- cache hit, false

box.begin()
s:select{}
box.commit()
stat_changed()  -- cache miss, true

s:select{}
stat_changed() -- cache hit, false

s:drop()

s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {parts = {1, 'uint', 2, 'uint'}})

str = ''

s:replace{0, 0, 0}
s:replace{1, 1, 1, str}
s:replace{1, 2, 1, str}
s:replace{1, 3, 1, str}
s:replace{1, 4, 1, str}
s:replace{2, 1, 2, str}
s:replace{2, 2, 2, str}
s:replace{2, 3, 2, str}
s:replace{2, 4, 2, str}
s:replace{3, 3, 4}

box.snapshot()
_ = stat_changed() -- init

box.begin()
s:select{}
box.commit()
stat_changed()  -- cache miss, true

s:get{1, 2}
stat_changed() -- cache hit, false

s:select{1}
stat_changed() -- cache hit, false

s:select{}
stat_changed() -- cache hit, false

s:drop()

s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {parts = {1, 'uint', 2, 'uint'}})

str = ''

s:replace{0, 0, 0}
s:replace{1, 2, 1, str}
s:replace{1, 3, 1, str}
s:replace{1, 4, 1, str}
s:replace{2, 1, 2, str}
s:replace{2, 2, 2, str}
s:replace{2, 3, 2, str}
s:replace{2, 4, 2, str}
s:replace{3, 3, 4}

box.begin()
s:select{1}
box.commit()

s:replace{1, 1, 1, str}

s:select{1}

s:drop()


s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {parts = {1, 'uint', 2, 'uint'}})

s:replace{1, 1, 1}
s:replace{2, 2, 2}
s:replace{3, 3, 3}
s:replace{4, 4, 4}
s:replace{5, 5, 5}

box.begin()
pk:min()
pk:max()
box.commit()

s:replace{0, 0, 0}
s:replace{6, 6, 6}

pk:min()
pk:max()

s:drop()

-- Same test w/o begin/end

s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk')

str = string.rep('!', 100)

for i = 1,1000 do s:insert{i, str} end
box.snapshot()

t = s:select{}

#t

t = s:replace{100, str}

for i = 1,10 do t = s:select{} end

t = s:replace{200, str}

s:drop()

s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {parts = {1, 'uint', 2, 'uint'}})

str = ''

s:replace{0, 0, 0}
s:replace{1, 1, 1, str}
s:replace{1, 2, 1, str}
s:replace{1, 3, 1, str}
s:replace{1, 4, 1, str}
s:replace{2, 1, 2, str}
s:replace{2, 2, 2, str}
s:replace{2, 3, 2, str}
s:replace{2, 4, 2, str}
s:replace{3, 3, 4}

box.snapshot()
_ = stat_changed() -- init

s:get{1, 2}
stat_changed()  -- cache miss, true

s:get{1, 2}
stat_changed() -- cache hit, false

s:select{1}
stat_changed()  -- cache miss, true

s:select{1}
stat_changed() -- cache hit, false

s:select{}
stat_changed()  -- cache miss, true

s:select{}
stat_changed() -- cache hit, false

s:drop()

s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {parts = {1, 'uint', 2, 'uint'}})

str = ''

s:replace{0, 0, 0}
s:replace{1, 1, 1, str}
s:replace{1, 2, 1, str}
s:replace{1, 3, 1, str}
s:replace{1, 4, 1, str}
s:replace{2, 1, 2, str}
s:replace{2, 2, 2, str}
s:replace{2, 3, 2, str}
s:replace{2, 4, 2, str}
s:replace{3, 3, 4}

box.snapshot()
_ = stat_changed() -- init

s:select{}
stat_changed()  -- cache miss, true

s:get{1, 2}
stat_changed() -- cache hit, false

s:select{1}
stat_changed() -- cache hit, false

s:select{}
stat_changed() -- cache hit, false

s:drop()

s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {parts = {1, 'uint', 2, 'uint'}})

str = ''

s:replace{0, 0, 0}
s:replace{1, 2, 1, str}
s:replace{1, 3, 1, str}
s:replace{1, 4, 1, str}
s:replace{2, 1, 2, str}
s:replace{2, 2, 2, str}
s:replace{2, 3, 2, str}
s:replace{2, 4, 2, str}
s:replace{3, 3, 4}

s:select{1}

s:replace{1, 1, 1, str}

s:select{1}

s:drop()


s = box.schema.space.create('test', {engine = 'vinyl'})
pk = s:create_index('pk', {parts = {1, 'uint', 2, 'uint'}})

s:replace{1, 1, 1}
s:replace{2, 2, 2}
s:replace{3, 3, 3}
s:replace{4, 4, 4}
s:replace{5, 5, 5}

pk:min()
pk:max()

s:replace{0, 0, 0}
s:replace{6, 6, 6}

pk:min()
pk:max()

s:drop()

-- https://github.com/tarantool/tarantool/issues/2189

local_space = box.schema.space.create('test', {engine='vinyl'})
pk = local_space:create_index('pk')
local_space:replace({1, 1})
local_space:replace({2, 2})
local_space:select{}
box.begin()
local_space:replace({1})
local_space:select{}
box.commit()
local_space:select{}
local_space:drop()

--
-- gh-2661: vy_cache_next_key after version change returns the
-- same statement as before.
--

s = box.schema.create_space('test', {engine = 'vinyl'})
pk = s:create_index('pk')
sk = s:create_index('sec', {parts = {2, 'string'}, unique = false})
s:insert{1, 'key1'}
sk:select('key1')
s:insert{3, 'key2'}
sk:select('key2')
s:insert{5, 'key1'}
sk:select('key1')
s:drop()

--
-- gh-2789: vy_cache_iterator must stop iteration, if a sought
-- statement does not exist and is between chained statements.
--
s = box.schema.create_space('test', {engine = 'vinyl'})
pk = s:create_index('pk')
s:replace{1}
s:replace{2}
s:replace{4}
s:replace{5}
box.snapshot()
-- Cache is not updated in autocommit mode.
box.begin() s:select{} box.commit()
info = pk:info().cache
info.lookup
info.get.rows
pk:info().disk.iterator.lookup
s:get{3}
info = pk:info().cache
info.lookup
info.get.rows
pk:info().disk.iterator.lookup
s:drop()

--
-- Cache resize
--
vinyl_cache = box.cfg.vinyl_cache
box.cfg{vinyl_cache = 1000 * 1000}
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
for i = 1, 100 do s:replace{i, string.rep('x', 1000)} end
for i = 1, 100 do s:get{i} end
box.info.vinyl().cache.used
box.cfg{vinyl_cache = 50 * 1000}
box.info.vinyl().cache.used
box.cfg{vinyl_cache = 0}
box.info.vinyl().cache.used
-- Make sure cache is not populated if box.cfg.vinyl_cache is set to 0
st1 = s.index.pk:info().cache
#s:select()
for i = 1, 100 do s:get{i} end
st2 = s.index.pk:info().cache
st2.put.rows - st1.put.rows
box.info.vinyl().cache.used
s:drop()
box.cfg{vinyl_cache = vinyl_cache}
