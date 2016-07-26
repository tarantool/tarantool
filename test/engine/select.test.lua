test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

-- select (str)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'str'} })
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
index = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })
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
index = space:create_index('primary', { type = 'tree', parts = {1, 'num', 2, 'num'} })
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
index = space:create_index('primary', { type = 'tree', parts = {1, 'str'} })
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
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'str'} })
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
index1 = space:create_index('primary', { type = 'tree', parts = {1, 'num'} })
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'int'} })
index3 = space:create_index('third', { type = 'tree', parts = {3, 'int'} })
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
