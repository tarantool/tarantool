env = require('test_run')
test_run = env.new()

engine = 'vinyl'

-- test duplicate conflict in the primary index
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')
space:insert({1})
space:insert({2})
space:insert({3})
space:select{}
space:insert({1})
space:select{}

box.begin()
space:insert({5})
space:insert({6})
space:insert({7})
space:insert({7})
space:insert({8})
box.commit()
index:select{}
index:get({1})
index:get({2})
index:get({3})
index:get({4})
index:get({5})
index:get({6})
index:get({7})
index:get({8})
space:drop()

-- test duplicate conflict in the secondary index
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { parts = {1, 'uint'} })
index2 = space:create_index('secondary', { parts = {2, 'int', 3, 'str'} })
space:insert({1})
space:insert({1, 1, 'a'})
space:insert({2, 2, 'a'})
space:insert({3, 2, 'b'})
space:insert({2, 3, 'c'})
index:select{}
index2:select{}
-- fail all
box.begin()
space:insert({1, 10, '10'})
space:insert({2, 10, '10'})
space:insert({3, 10, '10'})
box.commit()
index:select{}
index2:select{}

-- fail at the begining
box.begin()
space:insert({1, 1, '1'})
space:insert({4, 4, 'd'})
space:insert({5, 5, 'd'})
box.commit()
index:select{}
index2:select{}

-- fail at the end
box.begin()
space:insert({6, 6, 'd'})
space:insert({7, 6, 'e'})
space:insert({1, 1, '1'})
box.commit()
index:select{}
index2:select{}

-- fail pk
box.begin()
space:insert({1, 100, '100'})
box.commit()
index:select{}
index2:select{}

-- fail secondary
box.begin()
space:insert({8, 6, 'd'})
box.commit()
index:select{}
index2:select{}
space:drop()

-- test other operations (update, delete, upsert)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary')
space:insert({1})
space:insert({2})
space:insert({3})
space:select{}
box.begin()
space:insert({5})
index:update({1}, {{'+', 1, 3}})
box.commit()
index:select{}

box.begin()
space:delete({5})
space:update({1}, {{'=', 2, 43}})
space:insert({10})
space:upsert({3}, {{}, {'='}}) -- incorrect ops
space:insert({15})
box.commit()
index:select{}

box.begin()
space:delete({15})
space:delete({10})
space:insert({11})
space:upsert({12}, {})
space:insert({'abc'})
space:update({1}, {{'#', 2, 1}})
box.commit()
space:select{}

space:drop()

-- test same on several indexes
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { parts = {1, 'unsigned', 2, 'string'} })
index2 = space:create_index('secondary', { parts = {2, 'string', 3, 'scalar'}, unique = false })
index3 = space:create_index('third', { parts = {4, 'integer', 2, 'string'} })
space:insert({1, 'a', 'sclr1', 20})
space:insert({1, 'b', 'sclr1', 20})
space:insert({1, 'c', 'sclr1', -30})
space:insert({2, 'a', true, 15})
index:select{}
index2:select{}
index3:select{}

box.begin()
space:insert({1, 'a', 'sclr1', 20})
space:update({2, 'a'}, {{'=', 3, 3.14}})
box.commit()
index:select{}
index2:select{}
index3:select{}

box.begin()
space:delete({1, 'a'})
space:insert({100, '100', '100', 100})
space:update({2, 'a'}, {{}})
box.commit()
index:select{}
index2:select{}
index3:select{}

space:drop()

-- test rollback
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { parts = {1, 'unsigned'} })
index2 = space:create_index('secondary', { parts = {2, 'unsigned'}, unique = false })
index3 = space:create_index('third', { parts = {2, 'unsigned', 3, 'scalar'} })
space:insert({1, 1, 'a'})
space:insert({2, 1, 'b'})
space:insert({3, 2, 'a'})
index:select{}
index2:select{}
index3:select{}

box.begin()
space:insert({4, 2, 'b'})
space:upsert({2}, {{'=', 4, 1000}})
index3:delete({3, 'a'})
space:insert({4, 100, 100})
box.rollback()

index:select{}
index2:select{}
index3:select{}

space:drop()

