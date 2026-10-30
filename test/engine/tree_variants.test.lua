env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')

space = box.schema.space.create('tweedledum', { engine = engine })
i0 = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'}, unique = true })
i1 = space:create_index('i1', { type = 'tree', parts = {2, 'unsigned'}, unique = false })
i2 = space:create_index('i2', { type = 'tree', parts = {3, 'unsigned'}, unique = false })
i3 = space:create_index('i3', { type = 'tree', parts = {4, 'string', 5, 'string'}, unique = false })
i4 = space:create_index('i4', { type = 'tree', parts = {7, 'string', 6, 'string'}, unique = false })
i5 = space:create_index('i5', { type = 'tree', parts = {9, 'unsigned'}, unique = false })
i6 = space:create_index('i6', { type = 'tree', parts = {7, 'string', 6, 'string', 4, 'string', 5, 'string', 9, 'unsigned'}, unique = true })

space:insert{0, 0, 100, 'Joe', 'Sixpack', 'Drinks', 'Amstel', 'bar', 2000}
space:insert{1, 1, 200, 'Joe', 'Sixpack', 'Drinks', 'Heineken', 'bar', 2001}
space:insert{2, 2, 200, 'Joe', 'Sixpack', 'Drinks', 'Carlsberg', 'bar', 2002}
space:insert{3, 3, 300, 'Joe', 'Sixpack', 'Drinks', 'Corona Extra', 'bar', 2003}
space:insert{4, 4, 300, 'Joe', 'Sixpack', 'Drinks', 'Stella Artois', 'bar', 2004}
space:insert{5, 5, 300, 'Joe', 'Sixpack', 'Drinks', 'Miller Genuine Draft', 'bar', 2005}
space:insert{6, 6, 400, 'John', 'Smoker', 'Hits', 'A Pipe', 'foo', 2006}
space:insert{7, 7, 400, 'John', 'Smoker', 'Hits', 'A Bong', 'foo', 2007}
space:insert{8, 8, 400, 'John', 'Smoker', 'Rolls', 'A Joint', 'foo', 2008}
space:insert{9, 9, 400, 'John', 'Smoker', 'Rolls', 'A Blunt', 'foo', 2009}

-- In non-unique indexes select output order is undefined,
-- so it's better to additionally sort output to receive same order every time.
function sort_cmp(a, b) return a[1] < b[1] and true or false end
function sort(t) table.sort(t, sort_cmp) return t end

space.index['primary']:get{1}
sort(space.index['i1']:select{2})
sort(space.index[2]:select({300}))
#space.index['i3']:select({'Joe', 'Sixpack'})
#space.index['i3']:select('John')
#space.index['i4']:select('A Pipe')
{sort(space.index['i4']:select{'Miller Genuine Draft', 'Drinks'})}
sort(space.index['i5']:select{2007})
sort(space.index[6]:select{'Miller Genuine Draft', 'Drinks'})

tmp = space:delete{6}
tmp = space:delete{7}
tmp = space:delete{8}
tmp = space:delete{9}

space:insert{6, 6ULL, 400ULL, 'John', 'Smoker', 'Hits', 'A Pipe', 'foo', 2006}
space:insert{7, 7ULL, 400ULL, 'John', 'Smoker', 'Hits', 'A Bong', 'foo', 2007}
space:insert{8, 8ULL, 400ULL, 'John', 'Smoker', 'Rolls', 'A Joint', 'foo', 2008}
space:insert{9, 9ULL, 400ULL, 'John', 'Smoker', 'Rolls', 'A Blunt', 'foo', 2009}

sort(space.index['i1']:select{6ULL})
sort(space.index['i1']:select{6})
sort(space.index['i2']:select(400ULL))
sort(space.index['i2']:select(400))

sort(space:select{})

-- Test incorrect keys - supplied key field type does not match index type
-- https://bugs.launchpad.net/tarantool/+bug/1072624
space:insert{'', 1, 2, '', '', '', '', '', 0}
space:insert{'xxxxxxxx', 1, 2, '', '', '', '', '', 0}
space:insert{1, '', 2, '', '', '', '', '', 0}
space:insert{1, 'xxxxxxxxxxx', 2, '', '', '', '', '', 0}

space:drop()
sort = nil
sort_cmp = nil

-- test index bsize

space = box.schema.space.create('test', { engine = engine })
pk = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'string', 3, 'scalar'} })
pk:bsize() == 0
index2:bsize() == 0
space:insert({1, 'a', 3})
pk:bsize() > 0
index2:bsize() > 0

space:insert({2, 'b', 4})
old_pk_size = pk:bsize()
old_index2_size = index2:bsize()

space:insert({2, 'b', 4})
old_pk_size == pk:bsize()
old_index2_size == index2:bsize()

tmp = pk:delete({1})
pk:bsize() > 0
index2:bsize() > 0

tmp = index2:delete({'b', 4})
pk:bsize() > 0
index2:bsize() > 0

space:drop()

--range queries
space = box.schema.space.create('test', { engine = engine })
pk = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned', 2, 'unsigned'} })
str = require('string').rep('A', 500)
c1 = 100 c2 = 10
for i = 1,c1 do for j=1,c2 do space:insert{j, i, str} end end
good = true
for i = 1,c2 do if #space:select{i} ~= c1 then good = false end end
good --true
total_count = 0
for i = 1,c2 do total_count = total_count + #space:select{i} end
total_count --c1 * c2
space:drop()

