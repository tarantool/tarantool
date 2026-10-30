env = require('test_run')
test_run = env.new()

fail = false
old_tuple = nil
new_tuple = nil
function on_replace(old_tuple_, new_tuple_) if fail then old_tuple = nil new_tuple = nil error('fail') else old_tuple = old_tuple_ new_tuple = new_tuple_ end end

-- on insert one index
space = box.schema.space.create('test_space', { engine = 'vinyl' })
index = space:create_index('primary')

tmp = space:on_replace(on_replace)
space:insert({6, 'f'})
old_tuple, new_tuple
index:select{}
fail = true
space:insert({7, 'g'})
old_tuple, new_tuple
index:select{}
space:drop()
fail = false

-- on insert in multiple indexes
space = box.schema.space.create('test_space', { engine = 'vinyl' })
index = space:create_index('primary')
index2 = space:create_index('secondary', { parts = {2, 'scalar'} })
tmp = space:on_replace(on_replace)
space:insert({1, 2})
old_tuple, new_tuple
index:select{}
index2:select{}
fail = true
space:insert({2, 3})
old_tuple, new_tuple
index:select{}
index2:select{}
space:drop()
fail = false

-- on replace in one index
space = box.schema.space.create('test_space', { engine = 'vinyl' })
index = space:create_index('primary')
space:replace({1})
tmp = space:on_replace(on_replace)
space:replace({2})
old_tuple, new_tuple
space:replace({2})
old_tuple, new_tuple
space:replace({1, 43})
old_tuple, new_tuple
fail = true
space:replace({2, 100})
old_tuple, new_tuple
space:select{}
fail = false
space:drop()

-- ensure trigger error causes rollback of only one statement
fail = true
space = box.schema.space.create('test_space', { engine = 'vinyl' })
index = space:create_index('primary')
index2 = space:create_index('secondary', { parts = {2, 'string'} })
box.begin()
space:insert({1, 'a'})
space:insert({2, 'a'})
space:insert({1, 'b'})
space:insert({2, 'b'})
tmp = space:on_replace(on_replace)
space:insert({3, 'c'})
old_tuple, new_tuple
box.commit()
index:select{}
index2:select{}
fail = false
space:drop()

-- on replace in multiple indexes
space = box.schema.space.create('test_space', { engine = 'vinyl' })
index = space:create_index('primary')
index2 = space:create_index('secondary', { parts = {2, 'scalar'} })
tmp = space:on_replace(on_replace)
space:replace({1, 'a'})
space:replace({2, true})
space:replace({3, 36.6})
tmp = space:on_replace(on_replace)
space:replace({4, 4})
old_tuple, new_tuple
space:replace({5, 5})
old_tuple, new_tuple
space:replace({4, 5})
old_tuple, new_tuple
space:replace({5, 6, 60})
old_tuple, new_tuple
fail = true
space:replace({10, 10})
old_tuple, new_tuple
index:select{}
index2:select{}
fail = false
space:drop()

-- on delete from one index
space = box.schema.space.create('test_space', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert({1, 2})
space:insert({2, 3, 4})
space:insert({3, 4, 5})
space:insert({4})
tmp = space:on_replace(on_replace)
index:delete({3})
old_tuple, new_tuple
index:delete({4})
old_tuple, new_tuple
fail = true
index:delete({1})
old_tuple, new_tuple
index:select{}
fail = false
space:drop()

-- on delete from multiple indexes
space = box.schema.space.create('test_space', { engine = 'vinyl' })
index = space:create_index('primary')
index2 = space:create_index('secondary', { parts = {2, 'scalar'} })
space:insert({1, 'a'})
space:insert({2, 2, 'b'})
space:insert({3, 30.3})
space:insert({4, false})
tmp = space:on_replace(on_replace)
index:delete({1})
old_tuple, new_tuple
index2:delete({30.3})
old_tuple, new_tuple
fail = true
index2:delete({false})
old_tuple, new_tuple
index:select{}
index2:select{}
fail = false
space:drop()

-- on update one index
space = box.schema.space.create('test_space', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert({1, 2})
space:insert({2, 3, 4})
space:insert({3, 4, 5})
space:insert({4})
tmp = space:on_replace(on_replace)
index:update({1}, {{'#', 2, 1}})
old_tuple, new_tuple
index:update({2}, {{'#', 1, 1}}) -- must fail
old_tuple, new_tuple
index:update({3}, {{'=', 4, '300'}})
old_tuple, new_tuple
index:update({20}, {{'+', 2, 5}})
old_tuple, new_tuple
fail = true
index:update({1}, {{'=', 2, 'one'}})
old_tuple, new_tuple
index:select{}
fail = false
space:drop()

-- on update multiple indexes
space = box.schema.space.create('test_space', { engine = 'vinyl' })
index = space:create_index('primary')
index2 = space:create_index('secondary', { parts = {2, 'scalar'} })
space:insert({1, 'a'})
space:insert({2, 2, 'b'})
space:insert({3, 30.3})
space:insert({4, false})
tmp = space:on_replace(on_replace)
index:update({1}, {{'=', 2, 'z'}})
old_tuple, new_tuple
index:update({2}, {{'+', 1, 1}})
old_tuple, new_tuple
index2:update({30.3}, {{'+', 2, 10}})
old_tuple, new_tuple
index2:update({false}, {{'=', 3, 'equal false'}})
old_tuple, new_tuple
fail = true
index:update({1}, {{'=', 2, 'a'}})
old_tuple, new_tuple
index2:update({2}, {{'-', 2, 10}})
old_tuple, new_tuple
index:select{}
index2:select{}
fail = false
space:drop()

-- on upsert one index
space = box.schema.space.create('test_space', { engine = 'vinyl' })
index = space:create_index('primary')
space:insert({1, 1})
space:insert({2, 2, 2})
space:insert({3})
tmp = space:on_replace(on_replace)
space:upsert({1}, {{'+', 2, 10}})
old_tuple, new_tuple
space:upsert({4, 4, 4, 4}, {{'!', 2, 400}})
old_tuple, new_tuple
fail = true
space:upsert({2}, {{'!', 2, 2}})
old_tuple, new_tuple
space:upsert({5, 5, 5}, {{'!', 2, 5}})
old_tuple, new_tuple

index:select{}

fail = false
space:drop()

-- on upsert multiple indexes
space = box.schema.space.create('test_space', { engine = 'vinyl' })
index = space:create_index('primary', { parts = {1, 'unsigned', 2, 'unsigned'} })
index2 = space:create_index('secondary', { parts = {2, 'unsigned', 3, 'unsigned'} })
index3 = space:create_index('third', { parts = {3, 'unsigned'}, unique = false })
space:insert({1, 1, 1})
space:insert({2, 2, 2})
space:insert({3, 3, 3})
tmp = space:on_replace(on_replace)
space:upsert({1, 1, 1}, {{'+', 3, 1}})
old_tuple, new_tuple
space:upsert({1, 1, 1}, {{'+', 2, 1}}) -- must fail
old_tuple, new_tuple
space:upsert({4, 4, 4}, {{'!', 4, 400}})
old_tuple, new_tuple
index:select{}
index2:select{}
index3:select{}
fail = true
space:upsert({2, 2, 2}, {{'!', 4, 200}})
old_tuple, new_tuple
space:upsert({5, 5, 5}, {{'!', 4, 500}})
old_tuple, new_tuple
fail = false
space:drop()
