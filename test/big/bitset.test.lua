dofile('utils.lua')
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
------------------------------------------------------------------------------
-- BitsetIndex: BITS_ALL_SET (single bit)
------------------------------------------------------------------------------
dump(box.index.BITS_ALL_SET, 0)
dump(box.index.BITS_ALL_SET, 1)
dump(box.index.BITS_ALL_SET, 2)
dump(box.index.BITS_ALL_SET, 8)
dump(box.index.BITS_ALL_SET, 1073741824)
dump(box.index.BITS_ALL_SET, 2147483648)
dump(box.index.BITS_ALL_SET, 4294967296)
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
dump(box.index.BITS_ALL_NOT_SET, 2)
dump(box.index.BITS_ALL_NOT_SET, 8)
dump(box.index.BITS_ALL_NOT_SET, 4294967296)
------------------------------------------------------------------------------
-- BitsetIndex: BITS_ALL_NOT_SET (multiple bit)
------------------------------------------------------------------------------
dump(box.index.BITS_ALL_NOT_SET, 3)
dump(box.index.BITS_ALL_NOT_SET, 7)
dump(box.index.BITS_ALL_NOT_SET, 10)
dump(box.index.BITS_ALL_NOT_SET, 27)
dump(box.index.BITS_ALL_NOT_SET, 85)
dump(box.index.BITS_ALL_NOT_SET, 4294967295)
------------------------------------------------------------------------------
-- BitsetIndex: BITS_ANY_SET (single bit)
------------------------------------------------------------------------------
dump(box.index.BITS_ANY_SET, 0)
dump(box.index.BITS_ANY_SET, 16)
dump(box.index.BITS_ANY_SET, 128)
dump(box.index.BITS_ANY_SET, 4294967296)
------------------------------------------------------------------------------
-- BitsetIndex: BITS_ANY_SET (multiple bit)
------------------------------------------------------------------------------
dump(box.index.BITS_ANY_SET, 7)
dump(box.index.BITS_ANY_SET, 84)
dump(box.index.BITS_ANY_SET, 113)

drop_space()
-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
