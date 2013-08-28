
box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'str')
box.insert(box.schema.INDEX_ID, 0, 1, 'minmax', 'tree', 1, 2, 1, 'str', 2, 'str')
space = box.space[0]

space:insert('brave', 'new', 'world')
space.index[1]:min()
space.index[1]:max()
space:select(1, 'new', 'world')

-- A test case for Bug #904208
-- "assert failed, when key cardinality is greater than index cardinality"
--  https://bugs.launchpad.net/tarantool/+bug/904208

space:select(1, 'new', 'world', 'order')
space:delete('brave')

-- A test case for Bug #902091
-- "Positioned iteration over a multipart index doesn't work"
-- https://bugs.launchpad.net/tarantool/+bug/902091

space:insert('item 1', 'alabama', 'song')
space:select(1, 'alabama')
space:insert('item 2', 'california', 'dreaming ')
space:insert('item 3', 'california', 'uber alles')
space:insert('item 4', 'georgia', 'on my mind')
iter, tuple = space.index[1]:next('california')
tuple
_, tuple = space.index[1]:next(iter)
tuple
space:delete('item 1')
space:delete('item 2')
space:delete('item 3')
space:delete('item 4')
space:truncate()

--
-- Test that we print index number in error ER_INDEX_VIOLATION
--
space:insert(1, 'hello', 'world')
space:insert(2, 'hello', 'world')
space:drop()

--
-- Check range scan over multipart keys
--
box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'tree', 1, 1, 0, 'num64')
box.insert(box.schema.INDEX_ID, 0, 1, 'minmax', 'tree', 0, 2, 1, 'str', 2, 'str')
space = box.space[0]

space:insert('01234567', 'new', 'world')
space:insert('00000000', 'of', 'puppets')
space:insert('00000001', 'of', 'might', 'and', 'magic')
space:select_range(1, 2, 'of')
space:select_reverse_range(1, 2, 'of')
space:truncate()

--
-- A test case for Bug#1060967: truncation of 64-bit numbers
--

space:insert(2^51, 'hello', 'world')
space:select(0, 2^51)
space:drop()

--
-- Lua 64bit numbers support
--
box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'tree', 1, 1, 0, 'num64')
space = box.space[0]

space:insert(tonumber64('18446744073709551615'), 'magic')
tuple = space:select(0, tonumber64('18446744073709551615'))
num = box.unpack('l', tuple[0])
num
type(num) == 'cdata'
num == tonumber64('18446744073709551615')
num = box.unpack('l', tuple[0])
num == tonumber64('18446744073709551615')
space:delete(18446744073709551615ULL)
space:insert(125ULL, 'magic')
tuple = space:select(0, 125)
tuple2 = space:select(0, 125LL)
num = box.unpack('l', tuple[0])
num2 = box.unpack('l', tuple2[0])
{num, num2}
type(num) == 'cdata'
type(num2) == 'cdata'
num == tonumber64('125')
num2 == tonumber64('125')
space:truncate()

--
-- Tests for lua box.auto_increment with NUM64 keys
--
-- lua box.auto_increment() with NUM64 keys testing
box.auto_increment(space.n, 'a')
space:insert(tonumber64(5))
box.auto_increment(space.n, 'b')
box.auto_increment(space.n, 'c')

space:drop()

--
-- Lua select_reverse_range
--
-- lua select_reverse_range() testing
-- https://blueprints.launchpad.net/tarantool/+spec/backward-tree-index-iterator
box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'tree', 1, 1, 0, 'num')
box.insert(box.schema.INDEX_ID, 0, 1, 'range', 'tree', 1, 2, 1, 'num', 0, 'num')
space = box.space[0]

space:insert(0, 0)
space:insert(1, 0)
space:insert(2, 0)
space:insert(3, 0)
space:insert(4, 0)
space:insert(5, 0)
space:insert(6, 0)
space:insert(7, 0)
space:insert(8, 0)
space:insert(9, 0)
space:select_range(1, 10)
space:select_reverse_range(1, 10)
space:select_reverse_range(1, 4)
space:drop()

--
-- Tests for box.index iterators
--
box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'tree', 1, 1, 0, 'str')
box.insert(box.schema.INDEX_ID, 0, 1, 'i1', 'tree', 1, 2, 1, 'str', 2, 'str')
space = box.space[0]

pid = 1
tid = 999
-- setopt delimiter ';'
for sid = 1, 2 do
    for i = 1, 3 do
        space:insert('pid_'..pid, 'sid_'..sid, 'tid_'..tid)
        pid = pid + 1
        tid = tid - 1
    end
end;
-- setopt delimiter ''

index = space.index[1]

t = {}
for k, v in index.next, index, 'sid_1' do table.insert(t, v) end
t
t = {}
for k, v in index.prev, index, 'sid_2' do table.insert(t, v) end
t
t = {}
for k, v in index.next_equal, index, 'sid_1' do table.insert(t, v) end
t
t = {}
for k, v in index.prev_equal, index, 'sid_1' do table.insert(t, v) end
t
t = {}
for k, v in index.next_equal, index, 'sid_2' do table.insert(t, v) end
t
t = {}
for k, v in index.prev_equal, index, 'sid_2' do table.insert(t, v) end
t
t = {}
space:drop()

--
-- Tests for lua idx:count()
--
-- https://blueprints.launchpad.net/tarantool/+spec/lua-builtin-size-of-subtree
box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num')
box.insert(box.schema.INDEX_ID, 0, 1, 'i1', 'tree', 0, 2, 1, 'num', 2, 'num')
space = box.space[0]
space:insert(1, 1, 1)
space:insert(2, 2, 0)
space:insert(3, 2, 1)
space:insert(4, 3, 0)
space:insert(5, 3, 1)
space:insert(6, 3, 2)
space.index[1]:count(1)
space.index[1]:count(2)
space.index[1]:count(2, 1)
space.index[1]:count(2, 2)
space.index[1]:count(3)
space.index[1]:count(3, 3)
space.index[1]:count()
space:drop()

--
-- Tests for lua tuple:transform()
--
box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'str')
space = box.space[0]

t = space:insert('1', '2', '3', '4', '5', '6', '7')
t:transform(7, 0, '8', '9', '100')
t:transform(0, 1)
t:transform(1, 4)
t:transform(-1, 1)
t:transform(-3, 2)
t:transform(0, 0, 'A')
t:transform(-1, 0, 'A')
t:transform(0, 1, 'A')
t:transform(-1, 1, 'B')
t:transform(0, 2, 'C')
t:transform(2, 0, 'hello')
t:transform(0, -1, 'C')
t:transform(0, 100)
t:transform(-100, 1)
t:transform(0, 3, 1, 2, 3)
t:transform(3, 1, tonumber64(4))
t:transform(0, 1, {})
space:truncate()

--
-- Tests for lua tuple:find() and tuple:findall()
--
-- First space for hash_str tests

t = space:insert('A', '2', '3', '4', '3', '2', '5', '6', '3', '7')
t:find('2')
t:find('4')
t:find('5')
t:find('A')
t:find('0')

t:findall('A')
t:findall('2')
t:findall('3')
t:findall('0')

t:find(2, '2')
t:find(89, '2')
t:findall(4, '3')

t = space:insert('Z', '2', 2, 3, tonumber64(2))
t:find(2)
t:find(tonumber64(2))
t:find('2')
space:drop()

-- A test case for Bug #1038784
-- transform returns wrong tuple and put broken reply into socket
-- http://bugs.launchpad.net/tarantool/+bug/1038784
--  https://bugs.launchpad.net/tarantool/+bug/1006354
--  lua box.auto_increment() testing

box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'tree', 1, 1, 0, 'num')
space = box.space[0]
dofile('push.lua')

push_collection(space, 0, 1038784, 'hello')
push_collection(space, 0, 1038784, 'hello')
push_collection(space, 0, 1038784, 'hello')

push_collection(space, 1, 1038784, 'hi')
push_collection(space, 2, 1038784, 'hi')
push_collection(space, 2, 1038784, 'hi')

push_collection(space, 5, 1038784, 'hey')
push_collection(space, 5, 1038784, 'hey')
push_collection(space, 5, 1038784, 'hey')
push_collection(space, 5, 1038784, 'hey')

-- # lua box.auto_increment() testing
-- # http://bugs.launchpad.net/tarantool/+bug/1006354
--
-- Tests for lua box.auto_increment
--
space:truncate()
box.auto_increment(space.n, 'a')
space:insert(5)
box.auto_increment(space.n, 'b')
box.auto_increment(space.n, 'c')
space:drop()

-- A test case for Bug #1042798
-- Truncate hangs when primary key is not in linear or starts at the first field
-- https://bugs.launchpad.net/tarantool/+bug/1042798
--
box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'tree', 1, 2, 2, 'num', 1, 'num')
space = box.space[0]

-- Print key fields in pk
space.index[0].key_field
space:insert(1, 2, 3, 4)
space:insert(10, 20, 30, 40)
space:insert(20, 30, 40, 50)
space:select(0)

-- Truncate must not hang
space:truncate()

-- Empty result
space:select(0)
space:drop()
    
--
-- index:random test
-- 
dofile('index_random_test.lua')
box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'tree', 1, 1, 0, 'num')
box.insert(box.schema.INDEX_ID, 0, 1, 'primary', 'hash', 1, 1, 0, 'num')
space = box.space[0]
-------------------------------------------------------------------------------
-- TreeIndex::random()
-------------------------------------------------------------------------------

index_random_test(space, 0)

-------------------------------------------------------------------------------
-- HashIndex::random()
-------------------------------------------------------------------------------

index_random_test(space, 1)

space:drop()
space = nil

-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
