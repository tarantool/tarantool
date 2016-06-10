test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
test_run:cmd("push filter '"..engine.."' to 'engine'")

--------------------------------------------------------------------------------
-- Prefix search (test partially specified keys in TREE indexes)
--------------------------------------------------------------------------------

s1 = box.schema.space.create('tree_prefix_search', { engine = engine })
_ = s1:create_index('primary', { type = 'tree', parts = {1, 'str'}})

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
pk = space:create_index('primary', { type = 'tree', parts = {1, 'num'}})

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
-- single-part (string)
-------------------------------------------------------------------------------

space = box.schema.space.create('str', { engine = engine })
pk = space:create_index('primary', { type = 'tree', parts = {1, 'str'}})

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
pk = space:create_index('primary', { type = 'tree', parts = {1, 'num', 2, 'str'}})

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
pk = space:create_index('primary', { type = 'tree', parts = {1, 'str', 2, 'num'}})

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
-- gh-1467: invalid iterator type
-------------------------------------------------------------------------------

space = box.schema.space.create('invalid', { engine = engine })
pk = space:create_index('primary', { type = 'tree', parts = {1, 'str'}})

pk:select({}, {iterator = 'BITS_ALL_SET'})

space:drop()
space = nil
pk = nil

-------------------------------------------------------------------------------
-- Cleanup
-------------------------------------------------------------------------------

test_run:cmd("clear filter")
engine = nil
test_run = nil
