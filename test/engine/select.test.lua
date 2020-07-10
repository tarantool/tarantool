test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

-- select (str)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'string'} })
for key = 1, 100 do space:replace({tostring(key)}) end
index:select({}, {iterator = box.index.ALL})
index:select({}, {iterator = box.index.GE})
index:select(tostring(44), {iterator = box.index.GE})
index:select({}, {iterator = box.index.GT})
index:select(tostring(44), {iterator = box.index.GT})
index:select({}, {iterator = box.index.LE})
index:select(tostring(77), {iterator = box.index.LE})
index:select({}, {iterator = box.index.LT})
index:select(tostring(77), {iterator = box.index.LT})
space:drop()


-- select (num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
for key = 1, 100 do space:replace({key}) end
index:select({}, {iterator = box.index.ALL})
index:select({}, {iterator = box.index.GE})
index:select(44, {iterator = box.index.GE})
index:select({}, {iterator = box.index.GT})
index:select(44, {iterator = box.index.GT})
index:select({}, {iterator = box.index.LE})
index:select(77, {iterator = box.index.LE})
index:select({}, {iterator = box.index.LT})
index:select(77, {iterator = box.index.LT})
space:drop()


-- select multi-part (num, num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned', 2, 'unsigned'} })
for key = 1, 100 do space:replace({key, key}) end
index:select({}, {iterator = box.index.ALL})
index:select({}, {iterator = box.index.GE})
index:select({44, 44}, {iterator = box.index.GE})
index:select({}, {iterator = box.index.GT})
index:select({44, 44}, {iterator = box.index.GT})
index:select({}, {iterator = box.index.LE})
index:select({77, 77}, {iterator = box.index.LE})
index:select({}, {iterator = box.index.LT})
index:select({77, 77}, {iterator = box.index.LT})
space:drop()

-- select with box.tuple.new
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'string'} })
for key = 1, 100 do space:replace({tostring(key)}) end
index:select(box.tuple.new{}, {iterator = box.index.ALL})
index:select(box.tuple.new{}, {iterator = box.index.GE})
index:select(box.tuple.new(tostring(44)), {iterator = box.index.GE})
index:select(box.tuple.new{}, {iterator = box.index.GT})
index:select(box.tuple.new(tostring(44)), {iterator = box.index.GT})
index:select(box.tuple.new{}, {iterator = box.index.LE})
index:select(box.tuple.new(tostring(77)), {iterator = box.index.LE})
index:select(box.tuple.new{}, {iterator = box.index.LT})
index:select(box.tuple.new(tostring(77)), {iterator = box.index.LT})
space:drop()

-- select multiple indices

-- two indices
space = box.schema.space.create('test', { engine = engine })
index1 = space:create_index('primary', { type = 'tree', parts = {1, 'number'} })
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'string'} })
space:insert({1, 'a'})
space:insert({2, 'd'})
space:insert({3, 'c'})
space:insert({4, 'b'})
space:insert({5, 'bbbb'})
space:insert({5, 'cbcb'})
space:insert({6, 'bbbb'})
space:insert({-45.2, 'waerwe'})
index1:select{}
index2:select{}
space:get{5}
index1:get{5}
space:select{5}
index1:get{5}
index2:get{'a'}
index2:select{'a'}
space:drop()

-- three indices
space = box.schema.space.create('test', { engine = engine })
index1 = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'integer'} })
index3 = space:create_index('third', { type = 'tree', parts = {3, 'integer'} })
space:insert({1, -30, 9})
space:insert({5, 234, 9789})
space:insert({10, -56, 212})
space:insert({2, 762, 1235})
space:insert({4, 7873, 67545})
space:insert({9, 103, 1232})
index1:select{}
index2:select{}
index3:select{}
index1:select{10}
index1:get{9}
index2:select{-56}
index2:select{-57}
index2:get{103}
index2:get{104}
index3:get{9}
index3:select{1235}
space:drop()

--https://github.com/tarantool/tarantool/issues/5161
--formatted select
s = box.schema.space.create('test', { engine = engine })
i1 = s:create_index('test1')
i2 = s:create_index('test2', {parts={{2, 'unsigned'}}})
_ = s:replace{1, 2, 3, 4, box.NULL, 5}
_ = s:replace{3, 4, true, {1, 2, 3}}
_ = s:replace{5, 6, false, {1, 2, 3, ['key']='value'}}
_ = s:replace{3, 4, true, {1, {3, {aa=1,bb=2}}, 3}}
_ = s:replace{7, 8, 'asdgsdgswegg', 'sdf', 'dsgfsdgsegasges' }
s:fselect()
s:fselect({5}, {iterator='le'})
s:fselect({5}, {iterator='le', fselect_max_width=40})
i1:fselect({3})
i2:fselect({6})
i1:fselect({2})
i2:fselect({5})
s:format{{name='name', type='unsigned'}, {name='veeeeeeeery long name', type='unsigned'}}
s:fselect()
s:fselect({}, nil, {max_width=40})
i1:fselect({3})
i2:fselect({6})
i1:fselect({2})
i2:fselect({5})
s:fselect({}, {fselect_type='gh'})
s:fselect({}, nil, {type='gh'})
s:fselect({}, nil, {fselect_type='github'})
s:gselect{}
fselect_type = 'gh'
s:fselect()
fselect_type = nil
s:fselect()
s:fselect({}, {fselect_type='jira'})
s:jselect()
s:fselect({}, {fselect_widths={8, 8, nil, 10}})
s:fselect(nil, {fselect_use_nbsp=false})
