dofile('bitset.lua')

create_space()

------------------------------------------------------------------------------
-- BitsetIndex: insert/delete
------------------------------------------------------------------------------
test_insert_delete(128)

------------------------------------------------------------------------------
-- BitsetIndex: ALL
------------------------------------------------------------------------------
clear()
fill(1, 128)
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

drop_space()

------------------------------------------------------------------------------
-- Misc
------------------------------------------------------------------------------

-- gh-1467: invalid iterator type
space = box.schema.space.create('test')
_ = space:create_index('primary', { type = 'hash', parts = {1, 'num'}, unique = true })
_ = space:create_index('bitset', { type = 'bitset', parts = {2, 'num'}, unique = false })
space.index.bitset:select({1}, { iterator = 'OVERLAPS'})
space:drop()
space = nil
