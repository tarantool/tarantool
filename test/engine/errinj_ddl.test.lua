test_run = require('test_run').new()
fiber = require('fiber')
engine = test_run:get_cfg('engine')
errinj = box.error.injection

--
-- Check that ALTER is abroted if a tuple inserted during space
-- format change does not conform to the new format.
--
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'string', is_nullable = true}
s = box.schema.space.create('test', {engine = engine, format = format})
_ = s:create_index('pk')

pad = string.rep('x', 16)
for i = 101, 200 do s:replace{i, pad} end

ch = fiber.channel(1)
test_run:cmd("setopt delimiter ';'")
_ = fiber.create(function()
    fiber.sleep(0.01)
    for i = 1, 100 do
        s:replace{i, box.NULL}
    end
    errinj.set("ERRINJ_CHECK_FORMAT_DELAY", false)
    ch:put(true)
end);
test_run:cmd("setopt delimiter ''");

format[2].is_nullable = false
errinj.set("ERRINJ_CHECK_FORMAT_DELAY", true)
s:format(format) -- must fail
ch:get()

s:count() -- 200
s:drop()

t = box.schema.space.create("space", {engine = engine})
_ = t:create_index("pk")
for i = 1,1000 do t:insert{i, i} end

-- check that index build is aborted in case conflicting tuple is inserted
-- error injection makes index build yield after 1 tuple is inserted into
-- new index, check that index build is aborted if unique constraint is
-- violated, as well as if tuple format doesn't match the new indexes one.

test_run:cmd("setopt delimiter ';'")

errinj.set('ERRINJ_BUILD_INDEX_DELAY', true);
fiber.new(function() t:replace{1, 2} errinj.set('ERRINJ_BUILD_INDEX_DELAY',false) end)
t:create_index('sk', {parts = {2, 'unsigned'}, unique=true});

t:get{1};
t.index.sk;

t:replace{1, 1};

errinj.set('ERRINJ_BUILD_INDEX_DELAY', true);
fiber.new(function() t:replace{2, 3} errinj.set('ERRINJ_BUILD_INDEX_DELAY', false) end)
t:create_index('sk', {parts = {2, 'unsigned'}, unique=true});

t:get{2};
t.index.sk == nil;

t:replace{2, 2};

errinj.set('ERRINJ_BUILD_INDEX_DELAY', true);
fiber.new(function() t:replace{1, 'asdf'} errinj.set('ERRINJ_BUILD_INDEX_DELAY', false) end)
t:create_index('sk', {parts = {2, 'unsigned'}});

t:get{1};
t.index.sk == nil;

t:replace{1, 1};

errinj.set('ERRINJ_BUILD_INDEX_DELAY', true);
fiber.new(function() t:replace{2, 'asdf'} errinj.set('ERRINJ_BUILD_INDEX_DELAY', false) end)
t:create_index('sk', {parts = {2, 'unsigned'}});

t:get{2};
t.index.sk == nil;

t:replace{2, 2};

test_run:cmd("setopt delimiter ''");

t:drop()

--
-- gh-2449: change 'unique' index property from true to false
-- is done without index rebuild.
--
s = box.schema.space.create('test', { engine = engine })
_ = s:create_index('primary')
_ = s:create_index('secondary', {unique = true, parts = {2, 'unsigned'}})
s:insert{1, 10}
errinj.set("ERRINJ_BUILD_INDEX", 1);
s.index.secondary:alter{unique = false} -- ok
s.index.secondary.unique
s.index.secondary:alter{unique = true} -- error
s.index.secondary.unique
errinj.set("ERRINJ_BUILD_INDEX", -1);
s:insert{2, 10}
s.index.secondary:select(10)
s:drop()

--
-- Check that ALTER is aborted if a tuple inserted during index build
-- doesn't conform to the new format.
--
s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')

pad = string.rep('x', 16)
for i = 101, 200 do s:replace{i, i, pad} end

ch = fiber.channel(1)
test_run:cmd("setopt delimiter ';'")
_ = fiber.create(function()
    fiber.sleep(0.01)
    for i = 1, 100 do
        s:replace{i}
    end
    errinj.set("ERRINJ_BUILD_INDEX_DELAY", false)
    ch:put(true)
end);
test_run:cmd("setopt delimiter ''");

errinj.set("ERRINJ_BUILD_INDEX_DELAY", true)
s:create_index('sk', {parts = {2, 'unsigned'}}) -- must fail

ch:get()

s:count() -- 200
s:drop()

--
-- Check that ALTER is aborted if a tuple inserted during index build
-- violates unique constraint.
--
s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')

pad = string.rep('x', 16)
for i = 101, 200 do s:replace{i, i, pad} end

ch = fiber.channel(1)
test_run:cmd("setopt delimiter ';'")
_ = fiber.create(function()
    fiber.sleep(0.01)
    for i = 1, 100 do
        s:replace{i, i + 1}
    end
    errinj.set("ERRINJ_BUILD_INDEX_DELAY", false)
    ch:put(true)
end);
test_run:cmd("setopt delimiter ''");

errinj.set("ERRINJ_BUILD_INDEX_DELAY", true)
ok, err = pcall(s.create_index, s, 'sk', {parts = {2, 'unsigned'}})
assert(not ok)
assert(tostring(err):find('Duplicate key') ~= nil)

ch:get()

s:count() -- 200
s:drop()

--
-- Dropping and recreating a space while it is being written
-- to a checkpoint.
--
s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')
for i = 1, 5 do s:insert{i, i} end

errinj.set("ERRINJ_SNAP_WRITE_DELAY", true)
ch = fiber.channel(1)
_ = fiber.create(function() box.snapshot() ch:put(true) end)

s:drop()

s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')
for i = 1, 5 do s:insert{i, i * 10} end

errinj.set("ERRINJ_SNAP_WRITE_DELAY", false)
ch:get()

s:select()
s:drop()

--
-- Concurrent DDL.
--
s1 = box.schema.space.create('test1', {engine = engine})
_ = s1:create_index('pk')
for i = 1, 5 do s1:insert{i, i} end

s2 = box.schema.space.create('test2', {engine = engine})
_ = s2:create_index('pk')
for i = 1, 5 do s2:insert{i, i} end

errinj.set("ERRINJ_BUILD_INDEX_DELAY", true)
ch = fiber.channel(1)
_ = fiber.create(function() pcall(s1.create_index, s1, 'sk', {parts = {2, 'unsigned'}}) ch:put(true) end)

-- Modification of the same space must fail while an index creation
-- is in progress.
s1:format{{'a', 'unsigned'}, {'b', 'unsigned'}}
s1:truncate()
_ = s1:create_index('tk', {parts = {3, 'unsigned'}})
s1:drop()

-- Modification of another space must work though.
s2:format{{'a', 'unsigned'}, {'b', 'unsigned'}}
s2:truncate()
_ = s2:create_index('sk', {parts = {2, 'unsigned'}})
s2:drop()
s2 = box.schema.space.create('test2', {engine = engine})
_ = s2:create_index('pk')
s2:drop()

errinj.set("ERRINJ_BUILD_INDEX_DELAY", false)
ch:get()

s1.index.pk:select()
-- gh-5998: first DDL operation to be committed wins and aborts all other
-- transactions. So index creation should fail.
--
assert(s1.index.sk == nil)
s1:drop()
