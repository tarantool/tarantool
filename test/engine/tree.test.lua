test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
test_run:cmd("push filter '"..engine.."' to 'engine'")

--------------------------------------------------------------------------------
-- Prefix search (test partially specified keys in TREE indexes)
--------------------------------------------------------------------------------

s1 = box.schema.space.create('tree_prefix_search', { engine = engine })
_ = s1:create_index('primary', { type = 'tree', parts = {1, 'string'}})

_ = s1:replace{''}
_ = s1:replace{'abcd'}
_ = s1:replace{'abcda'}
_ = s1:replace{'abcda_'}
_ = s1:replace{'abcdb'}
_ = s1:replace{'abcdb_'}
_ = s1:replace{'abcdb__'}
_ = s1:replace{'abcdb___'}
_ = s1:replace{'abcdc'}
_ = s1:replace{'abcdc_'}
s1.index.primary:select('abcdb', { iterator = 'GE' })
s1.index.primary:select('', { iterator = 'GE' })
s1.index.primary:select('', { iterator = 'GT' })
s1.index.primary:select('', { iterator = 'LE' })
s1.index.primary:select('', { iterator = 'LT' })
s1:drop()
s1 = nil

-------------------------------------------------------------------------------
-- single-part (unsigned)
-------------------------------------------------------------------------------

space = box.schema.space.create('uint', { engine = engine })
pk = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'}})

for i=1,9 do space:replace{i} end

pk:select({}, { iterator = 'ALL' })
pk:select({}, { iterator = 'EQ' })
pk:select({}, { iterator = 'REQ' })
pk:select({}, { iterator = 'GE' })
pk:select({}, { iterator = 'GT' })
pk:select({}, { iterator = 'LE' })
pk:select({}, { iterator = 'LT' })

pk:select({0}, { iterator = 'EQ' })
pk:select({0}, { iterator = 'REQ' })
pk:select({0}, { iterator = 'LE' })
pk:select({0}, { iterator = 'LT' })

pk:select({1}, { iterator = 'EQ' })
pk:select({1}, { iterator = 'REQ' })
pk:select({1}, { iterator = 'LE' })
pk:select({1}, { iterator = 'LT' })

pk:select({5}, { iterator = 'EQ' })
pk:select({5}, { iterator = 'REQ' })
pk:select({5}, { iterator = 'GE' })
pk:select({5}, { iterator = 'GT' })
pk:select({5}, { iterator = 'LE' })
pk:select({5}, { iterator = 'LT' })

pk:select({9}, { iterator = 'EQ' })
pk:select({9}, { iterator = 'REQ' })
pk:select({9}, { iterator = 'GE' })
pk:select({9}, { iterator = 'GT' })

pk:select({10}, { iterator = 'EQ' })
pk:select({10}, { iterator = 'REQ' })
pk:select({10}, { iterator = 'GE' })
pk:select({10}, { iterator = 'GT' })

pk:get({})

pk:get({0})
pk:get({5})
pk:get({10})

pk:get({10, 15})

space:drop()
space = nil
pk = nil

-------------------------------------------------------------------------------
-- single-part sparse (unsigned)
-------------------------------------------------------------------------------

space = box.schema.space.create('sparse_uint', { engine = engine })
pk = space:create_index('primary', { type = 'tree', parts = {3, 'unsigned'}})

for i=1,9 do space:replace{'', 0, i} end
space:insert{'', 0, 1} -- conflict

pk:select({}, { iterator = 'ALL' })
pk:select({}, { iterator = 'EQ' })
pk:select({}, { iterator = 'REQ' })
pk:select({}, { iterator = 'GE' })
pk:select({}, { iterator = 'GT' })
pk:select({}, { iterator = 'LE' })
pk:select({}, { iterator = 'LT' })

pk:select({0}, { iterator = 'EQ' })
pk:select({0}, { iterator = 'REQ' })
pk:select({0}, { iterator = 'LE' })
pk:select({0}, { iterator = 'LT' })

pk:select({1}, { iterator = 'EQ' })
pk:select({1}, { iterator = 'REQ' })
pk:select({1}, { iterator = 'LE' })
pk:select({1}, { iterator = 'LT' })

pk:select({5}, { iterator = 'EQ' })
pk:select({5}, { iterator = 'REQ' })
pk:select({5}, { iterator = 'GE' })
pk:select({5}, { iterator = 'GT' })
pk:select({5}, { iterator = 'LE' })
pk:select({5}, { iterator = 'LT' })

pk:select({9}, { iterator = 'EQ' })
pk:select({9}, { iterator = 'REQ' })
pk:select({9}, { iterator = 'GE' })
pk:select({9}, { iterator = 'GT' })

pk:select({10}, { iterator = 'EQ' })
pk:select({10}, { iterator = 'REQ' })
pk:select({10}, { iterator = 'GE' })
pk:select({10}, { iterator = 'GT' })

pk:get({})

pk:get({0})
pk:get({5})
pk:get({10})

pk:get({10, 15})

space:drop()
space = nil
pk = nil

-------------------------------------------------------------------------------
-- single-part (string)
-------------------------------------------------------------------------------

space = box.schema.space.create('string', { engine = engine })
pk = space:create_index('primary', { type = 'tree', parts = {1, 'string'}})

for i=1,9 do space:replace{'0'..i} end

pk:select({}, { iterator = 'ALL' })
pk:select({}, { iterator = 'EQ' })
pk:select({}, { iterator = 'REQ' })
pk:select({}, { iterator = 'GE' })
pk:select({}, { iterator = 'GT' })
pk:select({}, { iterator = 'LE' })
pk:select({}, { iterator = 'LT' })

pk:select({'00'}, { iterator = 'EQ' })
pk:select({'00'}, { iterator = 'REQ' })
pk:select({'00'}, { iterator = 'LE' })
pk:select({'00'}, { iterator = 'LT' })

pk:select({'01'}, { iterator = 'EQ' })
pk:select({'01'}, { iterator = 'REQ' })
pk:select({'01'}, { iterator = 'LE' })
pk:select({'01'}, { iterator = 'LT' })

pk:select({'05'}, { iterator = 'EQ' })
pk:select({'05'}, { iterator = 'REQ' })
pk:select({'05'}, { iterator = 'GE' })
pk:select({'05'}, { iterator = 'GT' })
pk:select({'05'}, { iterator = 'LE' })
pk:select({'05'}, { iterator = 'LT' })

pk:select({'09'}, { iterator = 'EQ' })
pk:select({'09'}, { iterator = 'REQ' })
pk:select({'09'}, { iterator = 'GE' })
pk:select({'09'}, { iterator = 'GT' })

pk:select({'10'}, { iterator = 'EQ' })
pk:select({'10'}, { iterator = 'REQ' })
pk:select({'10'}, { iterator = 'GE' })
pk:select({'10'}, { iterator = 'GT' })

pk:get({})

pk:get({'0'})
pk:get({'5'})
pk:get({'10'})

pk:get({'10', '15'})

space:drop()
space = nil
pk = nil

-------------------------------------------------------------------------------
-- multi-part (unsigned + string)
-------------------------------------------------------------------------------

space = box.schema.space.create('uint_str', { engine = engine })
pk = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned', 2, 'string'}})

for i=1,9 do for j=1,3 do space:replace({i, '0'..j}) end end

--
-- one part
--

pk:select({}, { iterator = 'ALL' })
pk:select({}, { iterator = 'EQ' })
pk:select({}, { iterator = 'REQ' })
pk:select({}, { iterator = 'GE' })
pk:select({}, { iterator = 'GT' })
pk:select({}, { iterator = 'LE' })
pk:select({}, { iterator = 'LT' })

pk:select({0}, { iterator = 'EQ' })
pk:select({0}, { iterator = 'REQ' })
pk:select({0}, { iterator = 'LE' })
pk:select({0}, { iterator = 'LT' })

pk:select({1}, { iterator = 'EQ' })
pk:select({1}, { iterator = 'REQ' })
pk:select({1}, { iterator = 'LE' })
pk:select({1}, { iterator = 'LT' })

pk:select({9}, { iterator = 'EQ' })
pk:select({9}, { iterator = 'REQ' })
pk:select({9}, { iterator = 'GE' })
pk:select({9}, { iterator = 'GT' })

pk:select({10}, { iterator = 'EQ' })
pk:select({10}, { iterator = 'REQ' })
pk:select({10}, { iterator = 'GE' })
pk:select({10}, { iterator = 'GT' })

pk:get({})
pk:get({'5'})

--
-- two parts
--

pk:select({5, '00'}, { iterator = 'EQ' })
pk:select({5, '00'}, { iterator = 'REQ' })
pk:select({5, '00'}, { iterator = 'GE' })
pk:select({5, '00'}, { iterator = 'GT' })
pk:select({5, '00'}, { iterator = 'LE' })
pk:select({5, '00'}, { iterator = 'LT' })

pk:select({5, '01'}, { iterator = 'EQ' })
pk:select({5, '01'}, { iterator = 'REQ' })
pk:select({5, '01'}, { iterator = 'GE' })
pk:select({5, '01'}, { iterator = 'GT' })
pk:select({5, '01'}, { iterator = 'LE' })
pk:select({5, '01'}, { iterator = 'LT' })

pk:select({5, '03'}, { iterator = 'EQ' })
pk:select({5, '03'}, { iterator = 'REQ' })
pk:select({5, '03'}, { iterator = 'GE' })
pk:select({5, '03'}, { iterator = 'GT' })
pk:select({5, '03'}, { iterator = 'LE' })
pk:select({5, '03'}, { iterator = 'LT' })

pk:select({5, '04'}, { iterator = 'EQ' })
pk:select({5, '04'}, { iterator = 'REQ' })
pk:select({5, '04'}, { iterator = 'GE' })
pk:select({5, '04'}, { iterator = 'GT' })
pk:select({5, '04'}, { iterator = 'LE' })
pk:select({5, '04'}, { iterator = 'LT' })

pk:get({4, '05'})
pk:get({4, '03'})

pk:get({4, '03', 100})

space:drop()
space = nil
pk = nil

-------------------------------------------------------------------------------
-- multi-part (string + unsigned)
-------------------------------------------------------------------------------

space = box.schema.space.create('str_uint', { engine = engine })
pk = space:create_index('primary', { type = 'tree', parts = {1, 'string', 2, 'unsigned'}})

for i=1,9 do for j=1,3 do space:replace({'0'..i, j}) end end

--
-- one part
--

pk:select({}, { iterator = 'ALL' })
pk:select({}, { iterator = 'EQ' })
pk:select({}, { iterator = 'REQ' })
pk:select({}, { iterator = 'GE' })
pk:select({}, { iterator = 'GT' })
pk:select({}, { iterator = 'LE' })
pk:select({}, { iterator = 'LT' })

pk:select({'00'}, { iterator = 'EQ' })
pk:select({'00'}, { iterator = 'REQ' })
pk:select({'00'}, { iterator = 'LE' })
pk:select({'00'}, { iterator = 'LT' })

pk:select({'01'}, { iterator = 'EQ' })
pk:select({'01'}, { iterator = 'REQ' })
pk:select({'01'}, { iterator = 'LE' })
pk:select({'01'}, { iterator = 'LT' })

pk:select({'09'}, { iterator = 'EQ' })
pk:select({'09'}, { iterator = 'REQ' })
pk:select({'09'}, { iterator = 'GE' })
pk:select({'09'}, { iterator = 'GT' })

pk:select({'10'}, { iterator = 'EQ' })
pk:select({'10'}, { iterator = 'REQ' })
pk:select({'10'}, { iterator = 'GE' })
pk:select({'10'}, { iterator = 'GT' })

pk:get({})
pk:get({'00'})
pk:get({'05'})
pk:get({'10'})

--
-- two parts
--

pk:select({'05', 0}, { iterator = 'EQ' })
pk:select({'05', 0}, { iterator = 'REQ' })
pk:select({'05', 0}, { iterator = 'GE' })
pk:select({'05', 0}, { iterator = 'GT' })
pk:select({'05', 0}, { iterator = 'LE' })
pk:select({'05', 0}, { iterator = 'LT' })

pk:select({'05', 1}, { iterator = 'EQ' })
pk:select({'05', 1}, { iterator = 'REQ' })
pk:select({'05', 1}, { iterator = 'GE' })
pk:select({'05', 1}, { iterator = 'GT' })
pk:select({'05', 1}, { iterator = 'LE' })
pk:select({'05', 1}, { iterator = 'LT' })

pk:select({'05', 3}, { iterator = 'EQ' })
pk:select({'05', 3}, { iterator = 'REQ' })
pk:select({'05', 3}, { iterator = 'GE' })
pk:select({'05', 3}, { iterator = 'GT' })
pk:select({'05', 3}, { iterator = 'LE' })
pk:select({'05', 3}, { iterator = 'LT' })

pk:select({'05', 4}, { iterator = 'EQ' })
pk:select({'05', 4}, { iterator = 'REQ' })
pk:select({'05', 4}, { iterator = 'GE' })
pk:select({'05', 4}, { iterator = 'GT' })
pk:select({'05', 4}, { iterator = 'LE' })
pk:select({'05', 4}, { iterator = 'LT' })

pk:get({'04', 5})
pk:get({'04', 3})

pk:get({'04', 3, 100})

space:drop()
space = nil
pk = nil

-------------------------------------------------------------------------------
-- multi-part sparse (string + unsigned)
-------------------------------------------------------------------------------

space = box.schema.space.create('sparse_str_uint', { engine = engine })
pk = space:create_index('primary', { type = 'tree', parts = {3, 'string', 1, 'unsigned'}})

for i=1,9 do for j=1,3 do space:replace({i, '', '0'..j}) end end

-- conflicts
space:insert({9, '', '01'})

--
-- one part
--

pk:select({}, { iterator = 'ALL' })
pk:select({}, { iterator = 'EQ' })
pk:select({}, { iterator = 'REQ' })
pk:select({}, { iterator = 'GE' })
pk:select({}, { iterator = 'GT' })
pk:select({}, { iterator = 'LE' })
pk:select({}, { iterator = 'LT' })

pk:select({'00'}, { iterator = 'EQ' })
pk:select({'00'}, { iterator = 'REQ' })
pk:select({'00'}, { iterator = 'LE' })
pk:select({'00'}, { iterator = 'LT' })

pk:select({'01'}, { iterator = 'EQ' })
pk:select({'01'}, { iterator = 'REQ' })
pk:select({'01'}, { iterator = 'LE' })
pk:select({'01'}, { iterator = 'LT' })

pk:select({'09'}, { iterator = 'EQ' })
pk:select({'09'}, { iterator = 'REQ' })
pk:select({'09'}, { iterator = 'GE' })
pk:select({'09'}, { iterator = 'GT' })

pk:select({'10'}, { iterator = 'EQ' })
pk:select({'10'}, { iterator = 'REQ' })
pk:select({'10'}, { iterator = 'GE' })
pk:select({'10'}, { iterator = 'GT' })

pk:get({})
pk:get({'00'})
pk:get({'05'})
pk:get({'10'})

--
-- two parts
--

pk:select({'05', 0}, { iterator = 'EQ' })
pk:select({'05', 0}, { iterator = 'REQ' })
pk:select({'05', 0}, { iterator = 'GE' })
pk:select({'05', 0}, { iterator = 'GT' })
pk:select({'05', 0}, { iterator = 'LE' })
pk:select({'05', 0}, { iterator = 'LT' })

pk:select({'05', 1}, { iterator = 'EQ' })
pk:select({'05', 1}, { iterator = 'REQ' })
pk:select({'05', 1}, { iterator = 'GE' })
pk:select({'05', 1}, { iterator = 'GT' })
pk:select({'05', 1}, { iterator = 'LE' })
pk:select({'05', 1}, { iterator = 'LT' })

pk:select({'05', 3}, { iterator = 'EQ' })
pk:select({'05', 3}, { iterator = 'REQ' })
pk:select({'05', 3}, { iterator = 'GE' })
pk:select({'05', 3}, { iterator = 'GT' })
pk:select({'05', 3}, { iterator = 'LE' })
pk:select({'05', 3}, { iterator = 'LT' })

pk:select({'05', 4}, { iterator = 'EQ' })
pk:select({'05', 4}, { iterator = 'REQ' })
pk:select({'05', 4}, { iterator = 'GE' })
pk:select({'05', 4}, { iterator = 'GT' })
pk:select({'05', 4}, { iterator = 'LE' })
pk:select({'05', 4}, { iterator = 'LT' })

pk:get({'04', 5})
pk:get({'04', 3})

pk:get({'04', 3, 100})

space:drop()
space = nil
pk = nil

-------------------------------------------------------------------------------
-- multiple indices
-------------------------------------------------------------------------------

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

-------------------------------------------------------------------------------
-- gh-1467: invalid iterator type
-------------------------------------------------------------------------------

space = box.schema.space.create('invalid', { engine = engine })
pk = space:create_index('primary', { type = 'tree', parts = {1, 'string'}})

pk:select({}, {iterator = 'BITS_ALL_SET'})

space:drop()
space = nil
pk = nil

-- Create and drop several indices
space = box.schema.space.create('test', { engine = engine })
pk = space:create_index('primary')
index2 = space:create_index('secondary', { parts = {2, 'str'} })
index3 = space:create_index('third', { parts = {3, 'str'}, unique = false })
index2:drop()
#box.space._index:select{space.id}
index4 = space:create_index('fourth', { parts = {2, 'str', 3, 'str'} })
index2 = space:create_index('secondary', { parts = {4, 'str'} })
#box.space._index:select{space.id}
index3:drop()
index2:drop()
index4:drop()
#box.space._index:select{space.id}
index2 = space:create_index('secondary', { parts = {2, 'str'} })
index3 = space:create_index('third', { parts = {3, 'str'}, unique = false })
index4 = space:create_index('fourth', { parts = {2, 'str', 3, 'str'} })
#box.space._index:select{space.id}
space:drop()

--Collation
s = box.schema.space.create('test')
i1 = s:create_index('i1', { type = 'tree', parts = {{1, 'str', collation='unicode'}}, unique = true })

_ = s:replace{"ааа"}
_ = s:replace{"еее"}
_ = s:replace{"ёёё"}
_ = s:replace{"жжж"}
_ = s:replace{"яяя"}
_ = s:replace{"ААА"}
_ = s:replace{"ЯЯЯ"}

-- good output
s:select{}

s:drop()

-------------------------------------------------------------------------------
-- Cleanup
-------------------------------------------------------------------------------

test_run:cmd("clear filter")
engine = nil
test_run = nil
