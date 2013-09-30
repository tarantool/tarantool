dofile('utils.lua')

box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 3, 0, 'num', 1, 'str', 2, 'num')
box.insert(box.schema.INDEX_ID, 0, 1, 'unique', 'hash', 1, 2, 2, 'num', 4, 'num')
hash = box.space[0]

-- insert rows
hash:insert(0, 'foo', 0, '', 1)
hash:insert(0, 'foo', 1, '', 1)
hash:insert(1, 'foo', 0, '', 2)
hash:insert(1, 'foo', 1, '', 2)
hash:insert(0, 'bar', 0, '', 3)
hash:insert(0, 'bar', 1, '', 3)
hash:insert(1, 'bar', 0, '', 4)
hash:insert(1, 'bar', 1, '', 4)

-- try to insert a row with a duplicate key
hash:insert(1, 'bar', 1, '', 5)

-- output all rows

--# setopt delimiter ';'
function box.select_all(space)
    local result = {}
    for k, v in box.space[space]:pairs() do
        table.insert(result, v)
    end
    return result
end;
--# setopt delimiter ''
box.sort(box.select_all(0))

-- primary index select
hash:select(0, 1, 'foo', 0)
hash:select(0, 1, 'bar', 0)
-- primary index select with missing part
hash:select(0, 1, 'foo')
-- primary index select with extra part
hash:select(0, 1, 'foo', 0, 0)
-- primary index select with wrong type
hash:select(0, 1, 'foo', 'baz')

-- secondary index select
hash:select(1, 1, 4)
-- secondary index select with no such key
hash:select(1, 1, 5)
-- secondary index select with missing part
hash:select(1, 1)
-- secondary index select with wrong type
hash:select(1, 1, 'baz')

-- cleanup
hash:truncate()
hash:len()
hash:drop()
