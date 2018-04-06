test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

-- space create/drop
space = box.schema.space.create('test', { engine = engine })
space:drop()


-- space index create/drop
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')
space:drop()


-- space index create/drop alter
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')
_index = box.space[box.schema.INDEX_ID]
_index:delete{102, 0}
space:drop()


-- space index create/drop tree string
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', {type = 'tree', parts = {1, 'string'}})
space:insert({'test'})
space:drop()


-- space index create/drop tree num
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', {type = 'tree', parts = {1, 'unsigned'}})
space:insert({13})
space:drop()


-- space index create/drop tree multi-part num
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', {type = 'tree', parts = {1, 'unsigned', 2, 'unsigned'}})
space:insert({13})
space:drop()


-- space index size
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')
primary = space.index[0]
primary:count()
space:insert({13})
space:insert({14})
space:insert({15})
primary:count()
space:drop()

-- Key part max
parts = {}
for i=1,box.schema.INDEX_PART_MAX,1 do parts[2 * i - 1] = i; parts[2 * i] = 'unsigned' end
space = box.schema.space.create('test', { engine = engine })
_ = space:create_index('primary', { type = 'tree', parts = parts })

tuple = {}
for i=1,box.schema.INDEX_PART_MAX,1 do tuple[i] = i; end
space:replace(tuple)
-- https://github.com/tarantool/tarantool/issues/1651 and https://github.com/tarantool/tarantool/issues/1671
-- space:upsert(tuple, {{'=', box.schema.INDEX_PART_MAX + 1, 100500}})
space:get(tuple)
space:select(tuple)
_ = space:delete(tuple)

space:drop()

-- Too many key parts
parts = {}
for i=1,box.schema.INDEX_PART_MAX + 1,1 do parts[2 * i - 1] = i; parts[2 * i] = 'unsigned' end
space = box.schema.space.create('test', { engine = engine })
_ = space:create_index('primary', { type = 'tree', parts = parts })
space:drop()

--
-- vy_mem of primary index contains statements with two formats.
--
space = box.schema.space.create('test1', { engine = engine })
pk = space:create_index('primary1')
idx2 = space:create_index('idx2', { parts = {2, 'unsigned'} })
space:replace({3, 8, 1})
idx2:select{}
space:get{3}
iter_obj = space:pairs(2, {iterator = 'GT'})
idx2:drop()
space:replace({4, 5, 6})
space:select{}
space:drop()

-- Change index name
space = box.schema.space.create('test', {engine = engine})
pk = space:create_index('pk')
space:replace{1}
space:replace{2}
space:replace{3}
box.space._index:select{space.id}[1][3]
pk:alter({name = 'altered_pk'})
box.space._index:select{space.id}[1][3]
space:drop()

--new index format
space = box.schema.space.create('test', {engine = engine})
pk = space:create_index('pk', {parts={{field1 = 1, type = 'unsigned'}}})
pk = space:create_index('pk', {parts={{field = 0, type = 'unsigned'}}})
pk = space:create_index('pk', {parts={{field = 1, type = 'const char *'}}})
pk = space:create_index('pk', {parts={{field = 1, type = 'unsigned'}}})
pk.parts
pk:drop()
pk = space:create_index('pk', {parts={{1, 'unsigned'}}})
pk.parts
pk:drop()
pk = space:create_index('pk', {parts={{1, type='unsigned'}}})
pk.parts
space:insert{1, 2, 3}
pk:drop()
space:drop()

--
-- gh-2893: inherit index part type from a format, if a parts array
-- is omited.
--
format = {{'field1', 'scalar'}}
s = box.schema.create_space('test', {format = format})
pk = s:create_index('pk')
pk.parts[1].type
s:drop()

-- Ensure type 'any' to be not inherited.
format = {{'field1'}}
s = box.schema.create_space('test', {format = format})
pk = s:create_index('pk')
pk.parts[1].type
s:drop()

--
-- gh-3229: update optionality if a space format is changed too,
-- not only when indexes are updated.
--
s = box.schema.create_space('test', {engine = engine})
format = {}
format[1] = {'field1', 'unsigned'}
format[2] = {'field2', 'unsigned', is_nullable = true}
format[3] = {'field3', 'unsigned'}
s:format(format)
pk = s:create_index('pk')
sk = s:create_index('sk', {parts = {{2, 'unsigned', is_nullable = true}}})
s:replace{2, 3, 4}
s:format({})
s:insert({1})
s:insert({4, 5})
s:insert({3, 4})
s:insert({0})
_ = s:delete({1})
s:select({})
pk:get({4})
sk:select({box.NULL})
sk:get({5})
s:drop()

--
-- Modify key definition without index rebuild.
--
s = box.schema.create_space('test', {engine = engine})
i1 = s:create_index('i1', {unique = true,  parts = {1, 'unsigned'}})
i2 = s:create_index('i2', {unique = false, parts = {2, 'unsigned'}})
i3 = s:create_index('i3', {unique = true,  parts = {3, 'unsigned'}})

_ = s:insert{1, 2, 3}
box.snapshot()
_ = s:insert{3, 2, 1}

i1:alter{parts = {1, 'integer'}}
_ = s:insert{-1, 2, 2}
i1:select()
i2:select()
i3:select()

i2:alter{parts = {2, 'integer'}}
i3:alter{parts = {3, 'integer'}}
_ = s:replace{-1, -1, -1}
i1:select()
i2:select()
i3:select()

box.snapshot()
_ = s:replace{-1, -2, -3}
_ = s:replace{-3, -2, -1}
i1:select()
i2:select()
i3:select()

s:drop()
