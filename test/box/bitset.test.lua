bset = require('bitset')

dump = bset.dump
bset.create_space()

------------------------------------------------------------------------------
-- BitsetIndex: insert/delete
------------------------------------------------------------------------------
bset.test_insert_delete(128)

------------------------------------------------------------------------------
-- BitsetIndex: ALL
------------------------------------------------------------------------------
bset.clear()
bset.fill(1, 128)
dump(box.index.BITS_ALL)
box.space.tweedledum.index.bitset:count()
------------------------------------------------------------------------------
-- BitsetIndex: BITS_ALL_SET (single bit)
------------------------------------------------------------------------------
dump(box.index.BITS_ALL_SET, 0)
box.space.tweedledum.index.bitset:count(0, { iterator = box.index.BITS_ALL_SET})
dump(box.index.BITS_ALL_SET, 1)
box.space.tweedledum.index.bitset:count(1, { iterator = box.index.BITS_ALL_SET})
dump(box.index.BITS_ALL_SET, 2)
box.space.tweedledum.index.bitset:count(2, { iterator = box.index.BITS_ALL_SET})
dump(box.index.BITS_ALL_SET, 8)
box.space.tweedledum.index.bitset:count(8, { iterator = box.index.BITS_ALL_SET})
dump(box.index.BITS_ALL_SET, 128)
box.space.tweedledum.index.bitset:count(128, { iterator = box.index.BITS_ALL_SET})
dump(box.index.BITS_ALL_SET, 1073741824)
box.space.tweedledum.index.bitset:count(1073741824, { iterator = box.index.BITS_ALL_SET})
dump(box.index.BITS_ALL_SET, 2147483648)
box.space.tweedledum.index.bitset:count(2147483648, { iterator = box.index.BITS_ALL_SET})

------------------------------------------------------------------------------
-- BitsetIndex: BITS_ALL_SET (multiple bit)
------------------------------------------------------------------------------
dump(box.index.BITS_ALL_SET, 3)
dump(box.index.BITS_ALL_SET, 7)
dump(box.index.BITS_ALL_SET, 31)
dump(box.index.BITS_ALL_SET, 5)
dump(box.index.BITS_ALL_SET, 10)
dump(box.index.BITS_ALL_SET, 27)
dump(box.index.BITS_ALL_SET, 341)
dump(box.index.BITS_ALL_SET, 2147483649)
dump(box.index.BITS_ALL_SET, 4294967295)
------------------------------------------------------------------------------
-- BitsetIndex: BITS_ALL_NOT_SET (single bit)
------------------------------------------------------------------------------
dump(box.index.BITS_ALL_NOT_SET, 0)
box.space.tweedledum.index.bitset:count(0, { iterator = box.index.BITS_ALL_NOT_SET})
dump(box.index.BITS_ALL_NOT_SET, 2)
box.space.tweedledum.index.bitset:count(2, { iterator = box.index.BITS_ALL_NOT_SET})
dump(box.index.BITS_ALL_NOT_SET, 8)
box.space.tweedledum.index.bitset:count(8, { iterator = box.index.BITS_ALL_NOT_SET})
dump(box.index.BITS_ALL_NOT_SET, 128)
box.space.tweedledum.index.bitset:count(128, { iterator = box.index.BITS_ALL_NOT_SET})
dump(box.index.BITS_ALL_NOT_SET, 1073741824)
box.space.tweedledum.index.bitset:count(1073741824, { iterator = box.index.BITS_ALL_NOT_SET})
dump(box.index.BITS_ALL_NOT_SET, 2147483648)
box.space.tweedledum.index.bitset:count(2147483648, { iterator = box.index.BITS_ALL_NOT_SET})

------------------------------------------------------------------------------
-- BitsetIndex: BITS_ALL_NOT_SET (multiple bit)
------------------------------------------------------------------------------
dump(box.index.BITS_ALL_NOT_SET, 3)
box.space.tweedledum.index.bitset:count(3, { iterator = box.index.BITS_ALL_NOT_SET})
dump(box.index.BITS_ALL_NOT_SET, 7)
box.space.tweedledum.index.bitset:count(7, { iterator = box.index.BITS_ALL_NOT_SET})
dump(box.index.BITS_ALL_NOT_SET, 10)
box.space.tweedledum.index.bitset:count(10, { iterator = box.index.BITS_ALL_NOT_SET})
dump(box.index.BITS_ALL_NOT_SET, 27)
box.space.tweedledum.index.bitset:count(27, { iterator = box.index.BITS_ALL_NOT_SET})
dump(box.index.BITS_ALL_NOT_SET, 85)
box.space.tweedledum.index.bitset:count(85, { iterator = box.index.BITS_ALL_NOT_SET})
dump(box.index.BITS_ALL_NOT_SET, 4294967295)
box.space.tweedledum.index.bitset:count(4294967295, { iterator = box.index.BITS_ALL_NOT_SET})

------------------------------------------------------------------------------
-- BitsetIndex: BITS_ANY_SET (single bit)
------------------------------------------------------------------------------
dump(box.index.BITS_ANY_SET, 0)
box.space.tweedledum.index.bitset:count(0, { iterator = box.index.BITS_ANY_SET})
dump(box.index.BITS_ANY_SET, 16)
box.space.tweedledum.index.bitset:count(16, { iterator = box.index.BITS_ANY_SET})
dump(box.index.BITS_ANY_SET, 128)
box.space.tweedledum.index.bitset:count(128, { iterator = box.index.BITS_ANY_SET})

------------------------------------------------------------------------------
-- BitsetIndex: BITS_ANY_SET (multiple bit)
------------------------------------------------------------------------------
dump(box.index.BITS_ANY_SET, 7)
dump(box.index.BITS_ANY_SET, 84)
dump(box.index.BITS_ANY_SET, 113)

bset.drop_space()

------------------------------------------------------------------------------
-- Misc
------------------------------------------------------------------------------

-- gh-1467: invalid iterator type
space = box.schema.space.create('test')
_ = space:create_index('primary', { type = 'hash', parts = {1, 'unsigned'}, unique = true })
_ = space:create_index('bitset', { type = 'bitset', parts = {2, 'unsigned'}, unique = false })
space.index.bitset:select({1}, { iterator = 'OVERLAPS'})
space:drop()
space = nil

-- gh-1549: BITSET index with inappropriate types crashes in debug build
space = box.schema.space.create('test')
_ = space:create_index('primary', { type = 'hash', parts = {1, 'unsigned'}, unique = true })
_ = space:create_index('bitset', { type = 'bitset', parts = {2, 'number'}, unique = false })
space:drop()
space = nil

-- https://github.com/tarantool/tarantool/issues/1896 wrong countspace = box.schema.space.create('test')
s = box.schema.space.create('test')
_ = s:create_index('primary', { type = 'hash', parts = {1, 'unsigned'}, unique = true })
i = s:create_index('bitset', { type = 'bitset', parts = {2, 'unsigned'}, unique = false })
s:insert{1, 0}
s:insert{2, 0}
s:insert{3, 0}
s:insert{4, 2}
s:insert{5, 2}
s:insert{6, 3}
s:insert{7, 4}
s:insert{8, 5}
s:insert{9, 8}
#i:select(7, {iterator = box.index.BITS_ANY_SET})
i:count(7, {iterator = box.index.BITS_ANY_SET})
s:drop()
s = nil

-- https://github.com/tarantool/tarantool/issues/1946 BITS_ALL_SET crashes
s = box.schema.space.create('test')
_ = s:create_index('primary', { type = 'hash', parts = {1, 'unsigned'}, unique = true })
i = s:create_index('bitset', { type = 'bitset', parts = {2, 'unsigned'}, unique = false })
for i=1,10 do s:insert{i, math.random(8)} end
good = true
function is_good(key, opts) return #i:select({key}, opts) == i:count({key}, opts) end
function check(key, opts) good = good and is_good(key, opts) end
for j=1,100 do check(math.random(9) - 1) end
for j=1,100 do check(math.random(9) - 1, {iterator = box.index.BITS_ANY_SET}) end
for j=1,100 do check(math.random(9) - 1, {iterator = box.index.BITS_ALL_SET}) end
for j=1,100 do check(math.random(9) - 1, {iterator = box.index.BITS_ALL_NOT_SET}) end
good
s:drop()
s = nil

-- Bitset index cannot be multikey.
s = box.schema.space.create('test')
_ = s:create_index('primary')
_ = s:create_index('bitset', {type = 'bitset', parts = {{'[2][*]', 'unsigned'}}})
s:drop()

-- Bitset index can not use function.
s = box.schema.space.create('withdata')
lua_code = [[function(tuple) return tuple[1] + tuple[2] end]]
box.schema.func.create('s', {body = lua_code, is_deterministic = true, is_sandboxed = true})
_ = s:create_index('pk')
_ = s:create_index('idx', {type = 'bitset', func = box.func.s.id, parts = {{1, 'unsigned'}}})
s:drop()
box.schema.func.drop('s')
