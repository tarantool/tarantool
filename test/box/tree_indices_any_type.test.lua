s0 = box.schema.space.create('my_space1')
i0 = s0:create_index('my_space1_idx1', {type='TREE', parts={1, 'NUMBER'}})

s0:insert({10})
s0:insert({11})
s0:insert({12})
s0:insert({13})
s0:select{}

s0:insert({3})
s0:insert({4})
s0:insert({5})
s0:insert({6})
s0:select{}

s0:insert({-5})
s0:insert({-6})
s0:insert({-7})
s0:insert({-8})
s0:select{}

s0:insert({-10})
s0:insert({-11})
s0:insert({-12})
s0:insert({-13})
s0:select{}

s0:insert({3.5})
s0:insert({4.5})
s0:insert({5.5})
s0:select{}

s0:insert({-3.5})
s0:insert({-4.5})
s0:insert({-5.5})
s0:select{}

s0:drop()

s1 = box.schema.space.create('my_space2')
i1 = s1:create_index('my_space2_idx1', {type='TREE', parts={1, 'SCALAR'}})

s1:insert({10})
s1:insert({11})
s1:insert({12})
s1:insert({13})
s1:select{}

s1:insert({3})
s1:insert({4})
s1:insert({5})
s1:insert({6})
s1:select{}

s1:insert({'ffff'})
s1:insert({'gggg'})
s1:insert({'hhhh'})
s1:select{}

s1:insert({'aaaa'})
s1:insert({'bbbb'})
s1:insert({'cccc'})
s1:select{}

s1:insert({3.5})
s1:insert({4.5})
s1:insert({5.5})
s1:select{}

s1:insert({-3.5})
s1:insert({-4.5})
s1:insert({-5.5})
s1:select{}

s1:insert({true})
s1:insert({false})
s1:insert({1})
s1:insert({0})

s1:insert({'!!!!'})
s1:insert({'????'})
s1:select{}

s1:drop()

s2 = box.schema.space.create('my_space3')
i2_1 = s2:create_index('my_space3_idx1', {type='TREE', parts={1, 'SCALAR', 2, 'INT', 3, 'NUMBER'}})
i2_2 = s2:create_index('my_space3_idx2', {type='TREE', parts={4, 'STR', 5, 'SCALAR'}})

s2:insert({10, 1, -1, 'z', true})
s2:insert({11, 2, 2, 'g', false})
s2:insert({12, 3, -3, 'e', -100.5})
s2:insert({13, 4, 4, 'h', 200})
s2:select{}

s2:insert({3, 5, -5, 'w', 'strstr'})
s2:insert({4, 6, 6, 'q', ';;;;'})
s2:insert({5, 7, -7, 'c', '???'})
s2:insert({6, 8, 8, 'k', '!!!'})
s2:select{}

s2:insert({'ffff', 9, -9, 'm', '123'})
s2:insert({'gggg', 10, 10, 'r', '456'})
s2:insert({'hhhh', 11, -11, 'i', 55555})
s2:insert({'hhhh', 11, -10, 'i', 55556})
s2:insert({'hhhh', 11, -12, 'i', 55554})
s2:select{}

s2:insert({'aaaa', 12, 12, 'o', 333})
s2:insert({'bbbb', 13, -13, 'p', '123'})
s2:insert({'cccc', 14, 14, 'l', 123})
s2:select{}

s2:insert({3.5, 15, -15, 'n', 500})
s2:insert({4.5, 16, 16, 'b', 'ghtgtg'})
s2:insert({5.5, 17, -17, 'v', '"""""'})
s2:select{}

s2:insert({-3.5, 18, 18, 'x', '---'})
s2:insert({-4.5, 19, -19, 'a', 56.789})
s2:insert({-5.5, 20, 20, 'f', -138.4})
s2:select{}

s2:insert({true, 21, -21, 'y', 50})
s2:insert({false, 22, 22, 's', 60})

s2:insert({'!!!!', 23, -23, 'u', 0})
s2:insert({'????', 24, 24, 'j', 70})
s2:select{}

s2.index.my_space3_idx2:select{}

s2:drop()
