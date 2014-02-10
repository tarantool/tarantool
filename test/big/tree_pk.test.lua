dofile('utils.lua')

s0 = box.schema.create_space('tweedledum')
s0:create_index('primary', { type = 'tree', parts = {0, 'num'}, unique = true })

-- integer keys
s0:insert{1, 'tuple'}
box.snapshot()
s0:insert{2, 'tuple 2'}
box.snapshot()

s0:insert{3, 'tuple 3'}
s0.index['primary']:select{1}
s0.index['primary']:select{2}
s0.index['primary']:select{3}

-- Cleanup
s0:delete{1}
s0:delete{2}
s0:delete{3}

-- Test incorrect keys - supplied key field type does not match index type
-- https://bugs.launchpad.net/tarantool/+bug/1072624
s0:insert{'xxxxxxx'}
s0:insert{''}
s0:insert{'12'}

s1 = box.schema.create_space('tweedledee')
s1:create_index('primary', { type = 'tree', parts = {0, 'str'}, unique = true })

s2 = box.schema.create_space('alice')
s2:create_index('primary', { type = 'tree', parts = {0, 'str'}, unique = true })

-- string keys
s1:insert{'identifier', 'tuple'}
box.snapshot()
s1:insert{'second', 'tuple 2'}
box.snapshot()
s1.index['primary']:eselect('second', { limit = 100, iterator = 'GE' })
s1.index['primary']:eselect('identifier', { limit = 100, iterator = 'GE' })

s1:insert{'third', 'tuple 3'}
s1.index['primary']:select{'identifier'}
s1.index['primary']:select{'second'}
s1.index['primary']:select{'third'}

-- Cleanup
s1:delete{'identifier'}
s1:delete{'second'}
s1:delete{'third'}

--# setopt delimiter ';'
function crossjoin(space0, space1, limit)
    local result = {}
    for k0, v0 in space0:pairs() do
        for k1, v1 in space1:pairs() do
            if limit <= 0 then
                return result
            end
            newtuple = v0:totable()
            for _, v in v1:pairs() do
                table.insert(newtuple, v)
            end
            table.insert(result, box.tuple.new(newtuple))
            limit = limit - 1
        end
    end
    return result
end;
--# setopt delimiter ''

s2:insert{'1', 'tuple'}
s1:insert{'1', 'tuple'}
s1:insert{'2', 'tuple'}

crossjoin(s1, s1, 0)
crossjoin(s1, s1, 5)
crossjoin(s1, s1, 10000)
crossjoin(s1, s2, 10000)
s1:truncate()
s2:truncate()

-- Bug #922520 - select missing keys
s0:insert{200, 'select me!'}
s0.index['primary']:select{200}
s0.index['primary']:select{199}
s0.index['primary']:select{201}

-- Test partially specified keys in TREE indexes
s1:insert{'abcd'}
s1:insert{'abcda'}
s1:insert{'abcda_'}
s1:insert{'abcdb'}
s1:insert{'abcdb_'}
s1:insert{'abcdb__'}
s1:insert{'abcdb___'}
s1:insert{'abcdc'}
s1:insert{'abcdc_'}
box.sort(s1.index['primary']:eselect('abcdb', { limit = 3, iterator = 'GE' }))
s1:drop()
s1 = nil
s2:drop()
s2 = nil

--
-- tree::replace tests
--
s0:truncate()

s0:create_index('i1', { type = 'tree', parts = {1, 'num'}, unique = true })
s0:create_index('i2', { type = 'tree', parts = {2, 'num'}, unique = false })
s0:create_index('i3', { type = 'tree', parts = {3, 'num'}, unique = true })

s0:insert{0, 0, 0, 0}
s0:insert{1, 1, 1, 1}
s0:insert{2, 2, 2, 2}

-- OK
s0:replace{1, 1, 1, 1}
s0:replace{1, 10, 10, 10}
s0:replace{1, 1, 1, 1}
s0.index['primary']:select{10}
s0.index['i1']:select{10}
s0.index['i2']:select{10}
s0.index['i3']:select{10}
s0.index['primary']:select{1}
s0.index['i1']:select{1}
s0.index['i2']:select{1}
s0.index['i3']:select{1}

-- OK
s0:insert{10, 10, 10, 10}
s0:delete{10}
s0.index['primary']:select{10}
s0.index['i1']:select{10}
s0.index['i2']:select{10}
s0.index['i3']:select{10}


-- TupleFound (primary key)
s0:insert{1, 10, 10, 10}
s0.index['primary']:select{10}
s0.index['i1']:select{10}
s0.index['i2']:select{10}
s0.index['i3']:select{10}
s0.index['primary']:select{1}

-- TupleNotFound (primary key)
s0:replace{10, 10, 10, 10}
s0.index['primary']:select{10}
s0.index['i1']:select{10}
s0.index['i2']:select{10}
s0.index['i3']:select{10}

-- TupleFound (key #1)
s0:insert{10, 0, 10, 10}
s0.index['primary']:select{10}
s0.index['i1']:select{10}
s0.index['i2']:select{10}
s0.index['i3']:select{10}
s0.index['i1']:select{0}

-- TupleFound (key #1)
s0:replace{2, 0, 10, 10}
s0.index['primary']:select{10}
s0.index['i1']:select{10}
s0.index['i2']:select{10}
s0.index['i3']:select{10}
s0.index['i1']:select{0}

-- TupleFound (key #3)
s0:insert{10, 10, 10, 0}
s0.index['primary']:select{10}
s0.index['i1']:select{10}
s0.index['i2']:select{10}
s0.index['i3']:select{10}
s0.index['i3']:select{0}

-- TupleFound (key #3)
s0:replace{2, 10, 10, 0}
s0.index['primary']:select{10}
s0.index['i1']:select{10}
s0.index['i2']:select{10}
s0.index['i3']:select{10}
s0.index['i3']:select{0}

-- Non-Uniq test (key #2)
s0:insert{4, 4, 0, 4}
s0:insert{5, 5, 0, 5}
s0:insert{6, 6, 0, 6}
s0:replace{5, 5, 0, 5}
box.sort({s0.index['i2']:select{0}})
s0:delete{5}
box.sort({s0.index['i2']:select{0}})

s0:drop()
s0 = nil

-----------------------
-- Iterator corruption during data modification 1)skipping during deleteion
s = box.schema.create_space('test')
s:create_index('primary', { type = 'tree', parts = {0, 'num'}, unique = true })
ind = s.index['primary']

s:insert{4}
s:insert{2}
s:insert{6}
s:insert{1}
s:insert{3}
s:insert{5}
s:insert{7}
s:insert{8}

-- it seems that all elements will be deleted:
for t in ind:iterator() do s:delete{t[0]} end

-- but (oops) some elements are left in space:
iterate('test', 'primary', 0, 1)

-- cleanup
s:drop()
s = nil

-----------------------
-- Iterator corruption during data modification 2)skipping during insertion
s = box.schema.create_space('test')
s:create_index('primary', { type = 'tree', parts = {0, 'num'}, unique = true })
ind = s.index['primary']

s:insert{3}
s:insert{2}
s:insert{4} -- now you see me
s:insert{1}

itr = ind:iterator()
itr() -- 1
itr() -- 2
for i = 5,100 do s:insert{i} end
itr() -- 3
itr() -- now you don't

-- cleanup
s:drop()
s = nil


-----------------------
-- Iterator corruption during data modification 3)repeating during delete/insert
s = box.schema.create_space('test')
s:create_index('primary', { type = 'tree', parts = {0, 'num'}, unique = true })
ind = s.index['primary']

s:insert{2}
s:insert{1}
s:insert{3}

itr = ind:iterator()
itr() -- 1
s:delete{2}
s:insert{0}
itr() -- 1 again
itr() -- null

-- cleanup
s:drop()
s = nil
