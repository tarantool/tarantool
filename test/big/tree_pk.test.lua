dofile('utils.lua')

box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'tree', 1, 1, 0, 'num')

s0 = box.space[0]

-- integer keys
s0:insert(1, 'tuple')
box.snapshot()
s0:insert(2, 'tuple 2')
box.snapshot()

s0:insert(3, 'tuple 3')
s0:select(0, 1)
s0:select(0, 2)
s0:select(0, 3)

-- Cleanup
s0:delete(1)
s0:delete(2)
s0:delete(3)

-- Test incorrect keys - supplied key field type does not match index type
-- https://bugs.launchpad.net/tarantool/+bug/1072624
s0:insert('xxxxxxx')
s0:insert('')
s0:insert('12')

box.insert(box.schema.SPACE_ID, 1, 0, 'tweedledee')
box.insert(box.schema.INDEX_ID, 1, 0, 'primary', 'tree', 1, 1, 0, 'str')
s1 = box.space[1]

-- string keys
s1:insert('identifier', 'tuple')
box.snapshot()
s1:insert('second', 'tuple 2')
box.snapshot()
{s1:select_range(0, '100', 'second')}
{s1:select_range(0, '100', 'identifier')}

s1:insert('third', 'tuple 3')
s1:select(0, 'identifier')
s1:select(0, 'second')
s1:select(0, 'third')

-- Cleanup
s1:delete('identifier')
s1:delete('second')
s1:delete('third')

-- setopt delimiter ';'
function crossjoin(space0, space1, limit)
    local result = {}
    for k0, v0 in space0:pairs() do
        for k1, v1 in space1:pairs() do
            if limit <= 0 then
                return result
            end
            newtuple = {v0:unpack()}
            for _, v in v1:pairs() do
                table.insert(newtuple, v)
            end
            table.insert(result, box.tuple.new(newtuple))
            limit = limit - 1
        end
    end
    return result
end;
-- setopt delimiter ''

s0:insert(1, 'tuple')
s1:insert(1, 'tuple')
s1:insert(2, 'tuple')

crossjoin(s1, s1, 0)
crossjoin(s1, s1, 5)
crossjoin(s1, s1, 10000)
crossjoin(s1, s0, 10000)
s1:truncate()

-- Bug #922520 - select missing keys
s0:insert(200, 'select me!')
s0:select(0, 200)
s0:select(0, 199)
s0:select(0, 201)

-- Test partially specified keys in TREE indexes
s1:insert('abcd')
s1:insert('abcda')
s1:insert('abcda_')
s1:insert('abcdb')
s1:insert('abcdb_')
s1:insert('abcdb__')
s1:insert('abcdb___')
s1:insert('abcdc')
s1:insert('abcdc_')
box.sort({s1.index[0]:select_range(3, 'abcdb')})
s1:drop()

--
-- tree::replace tests
--
s0:truncate()
box.insert(box.schema.INDEX_ID, 0, 1, 'i1', 'tree', 1, 1, 1, 'num')
box.insert(box.schema.INDEX_ID, 0, 2, 'i2', 'tree', 0, 1, 2, 'num')
box.insert(box.schema.INDEX_ID, 0, 3, 'i3', 'tree', 1, 1, 3, 'num')

s0:insert(0, 0, 0, 0)
s0:insert(1, 1, 1, 1)
s0:insert(2, 2, 2, 2)

-- OK
s0:replace_if_exists(1, 1, 1, 1)
s0:replace_if_exists(1, 10, 10, 10)
s0:replace_if_exists(1, 1, 1, 1)
s0:select(0, 10)
s0:select(1, 10)
s0:select(2, 10)
s0:select(3, 10)
s0:select(0, 1)
s0:select(1, 1)
s0:select(2, 1)
s0:select(3, 1)

-- OK
s0:insert(10, 10, 10, 10)
s0:delete(10)
s0:select(0, 10)
s0:select(1, 10)
s0:select(2, 10)
s0:select(3, 10)


-- TupleFound (primary key)
s0:insert(1, 10, 10, 10)
s0:select(0, 10)
s0:select(1, 10)
s0:select(2, 10)
s0:select(3, 10)
s0:select(0, 1)

-- TupleNotFound (primary key)
s0:replace_if_exists(10, 10, 10, 10)
s0:select(0, 10)
s0:select(1, 10)
s0:select(2, 10)
s0:select(3, 10)

-- TupleFound (key #1)
s0:insert(10, 0, 10, 10)
s0:select(0, 10)
s0:select(1, 10)
s0:select(2, 10)
s0:select(3, 10)
s0:select(1, 0)

-- TupleFound (key #1)
s0:replace_if_exists(2, 0, 10, 10)
s0:select(0, 10)
s0:select(1, 10)
s0:select(2, 10)
s0:select(3, 10)
s0:select(1, 0)

-- TupleFound (key #3)
s0:insert(10, 10, 10, 0)
s0:select(0, 10)
s0:select(1, 10)
s0:select(2, 10)
s0:select(3, 10)
s0:select(3, 0)

-- TupleFound (key #3)
s0:replace_if_exists(2, 10, 10, 0)
s0:select(0, 10)
s0:select(1, 10)
s0:select(2, 10)
s0:select(3, 10)
s0:select(3, 0)

-- Non-Uniq test (key #2)
s0:insert(4, 4, 0, 4)
s0:insert(5, 5, 0, 5)
s0:insert(6, 6, 0, 6)
s0:replace_if_exists(5, 5, 0, 5)
box.sort({s0:select(2, 0)})
s0:delete(5)
box.sort({s0:select(2, 0)})

s0:drop()

-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
